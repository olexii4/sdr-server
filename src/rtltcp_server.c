/*
 * rtltcp_server.c — rtl_tcp-compatible TCP server for MSi SDR hardware.
 *
 * Implements the standard rtl_tcp wire protocol so any rtl_tcp client
 * (SDR++, sdrtouch, HDSDR, SDR#, gqrx …) can connect over the network
 * and receive raw IQ samples from an MSi2500/MSi001 USB dongle.
 *
 * Up to MAX_CLIENTS clients may connect simultaneously and all receive
 * the same live IQ stream.  Any client may send tuning commands; the
 * last-writer-wins for frequency / gain settings.
 *
 * Protocol (same as osmocom rtl_tcp):
 *   Server → Client on connect: 12-byte magic header
 *     "RTL0" (4 bytes) + tuner_type uint32 BE + gain_count uint32 BE
 *   Server → Client ongoing: continuous 8-bit unsigned IQ bytes
 *     sample pairs: I(0–255), Q(0–255), centre = 128
 *   Client → Server: 5-byte commands (uint8 cmd + uint32 param, big-endian)
 *
 * Supported commands:
 *   0x01  SET_FREQ        center frequency (Hz)
 *   0x02  SET_SAMPLE_RATE sampling rate (S/s)
 *   0x03  SET_GAIN_MODE   0=auto, 1=manual
 *   0x04  SET_GAIN        gain × 10 (dB), e.g. 400 = 40.0 dB
 *   0x05  SET_PPM         (ignored)
 *   0x08  SET_AGC         0=off, 1=on
 *
 * Stop:  Ctrl+C  (sends SIGINT → clean shutdown)
 *
 * Usage: rtltcp_server [-p port] [-d device_index] [-g gain_dB]
 *                      [-r sample_rate] [-f center_freq]
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <msisdr.h>

/* ── Configuration ───────────────────────────────────────────────── */
#define DEFAULT_PORT         1234
#define DEFAULT_DEVICE_INDEX 0
#define DEFAULT_GAIN         40       /* dB, 0..102 */
#define DEFAULT_SAMPLE_RATE  2400000  /* 2.4 MHz — matches SDR++ default */
#define DEFAULT_CENTER_FREQ  100000000

#define TUNER_TYPE           5        /* R820T — accepted by all clients */
#define GAIN_STEPS           29
#define SEND_BUF_SAMPLES     16384
#define MAX_CLIENTS          4        /* simultaneous connections */

/* ── Global device + RF state ────────────────────────────────────── */
typedef struct {
    msisdr_dev_t    *dev;
    volatile uint32_t center_freq;
    volatile uint32_t sample_rate;
    volatile int      gain_mode;
    volatile int      gain;
    /* DC IIR correction — same as sdr-msi-driver (alpha=0.998) */
    float             dc_avg_i;
    float             dc_avg_q;
} device_state_t;

static device_state_t g_dev;
static volatile int   g_stop = 0;

/* ── Client broadcast list ───────────────────────────────────────── */
typedef struct {
    int             sock;   /* -1 = empty */
    pthread_t       tid;
} client_slot_t;

static client_slot_t       g_clients[MAX_CLIENTS];
static pthread_mutex_t     g_clients_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Separate volatile array for signal handler use (no mutex needed).
 * Signal handler reads these and calls shutdown() — async-signal-safe.
 * Values are socket FDs (>= 0) or -1.  Written under g_clients_mtx by
 * the main/command threads; read lock-free by the signal handler. */
static volatile int        g_client_fds[MAX_CLIENTS];

/* Pre-allocated conversion buffer — avoids malloc in the hot callback path
 * (~150 calls/second).  Max n_samples per callback at 2.4 MHz, 512 KB buf:
 * 524288 bytes / 2 bytes per int16 = 262144 samples. */
#define U8BUF_MAX 262144
static uint8_t g_u8buf[U8BUF_MAX];
static pthread_mutex_t g_u8buf_mtx = PTHREAD_MUTEX_INITIALIZER;

/* ── rtl_tcp command ─────────────────────────────────────────────── */
typedef struct { uint8_t cmd; uint32_t param; } __attribute__((packed)) rtltcp_cmd_t;

/* ── USB async callback — broadcast IQ to all connected clients ──── */
static void msisdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
    (void)ctx;
    if (g_stop) { msisdr_cancel_async(g_dev.dev); return; }

    /* Apply DC IIR correction (alpha=0.998) matching sdr-msi-driver.
     * Removes the zero-IF DC spike at centre frequency. */
    int16_t *samples  = (int16_t *)buf;
    uint32_t n_samples = len / sizeof(int16_t);

    for (uint32_t i = 0; i + 1 < n_samples; i += 2) {
        float si = (float)samples[i];
        float sq = (float)samples[i + 1];
        g_dev.dc_avg_i = 0.998f * g_dev.dc_avg_i + 0.002f * si;
        g_dev.dc_avg_q = 0.998f * g_dev.dc_avg_q + 0.002f * sq;
        int32_t ci = (int32_t)(si - g_dev.dc_avg_i);
        int32_t cq = (int32_t)(sq - g_dev.dc_avg_q);
        if (ci >  32767) ci =  32767; else if (ci < -32768) ci = -32768;
        if (cq >  32767) cq =  32767; else if (cq < -32768) cq = -32768;
        samples[i]     = (int16_t)ci;
        samples[i + 1] = (int16_t)cq;
    }

    /* Convert int16 → uint8 using pre-allocated buffer (no malloc in hot path) */
    if (n_samples > U8BUF_MAX) n_samples = U8BUF_MAX;  /* safety clamp */
    pthread_mutex_lock(&g_u8buf_mtx);
    for (uint32_t i = 0; i < n_samples; i++)
        g_u8buf[i] = (uint8_t)((samples[i] >> 8) + 128);

    /* Broadcast to all connected clients */
    pthread_mutex_lock(&g_clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].sock < 0) continue;
        ssize_t sent = 0;
        while (sent < (ssize_t)n_samples) {
            ssize_t r = send(g_clients[i].sock,
                             g_u8buf + sent, (size_t)(n_samples - sent),
                             MSG_DONTWAIT);
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                g_clients[i].sock = -1; g_client_fds[i] = -1;
                break;
            }
            if (r == 0) { g_clients[i].sock = -1; g_client_fds[i] = -1; break; }
            sent += r;
        }
    }
    pthread_mutex_unlock(&g_clients_mtx);
    pthread_mutex_unlock(&g_u8buf_mtx);
}

/* ── Per-client command thread ───────────────────────────────────── */
typedef struct { int slot; int sock; } cmd_thread_arg_t;

static void *client_cmd_thread(void *arg) {
    cmd_thread_arg_t *a = (cmd_thread_arg_t *)arg;
    int slot = a->slot;
    int sock = a->sock;
    free(a);

    rtltcp_cmd_t cmd;
    while (recv(sock, &cmd, sizeof(cmd), MSG_WAITALL) == (ssize_t)sizeof(cmd)) {
        uint32_t param = ntohl(cmd.param);
        switch (cmd.cmd) {
            case 0x01: /* SET_FREQ */
                g_dev.center_freq = param;
                fprintf(stdout, "[client %d] set_freq %u Hz\n", slot, param);
                if (g_dev.dev) msisdr_set_center_freq(g_dev.dev, param);
                break;
            case 0x02: /* SET_SAMPLE_RATE */
                if (param != g_dev.sample_rate && g_dev.dev) {
                    /* Clamp to device limits.  The library silently clamps but
                     * we must track the ACTUAL applied rate — the client will
                     * demodulate at whatever it requested, so using a different
                     * rate causes robotic audio.  Always warn the user. */
                    uint32_t applied = param;
                    if (applied < 1300000)  applied = 1300000;
                    if (applied > 15000000) applied = 15000000;
                    if (applied != param)
                        fprintf(stderr,
                            "[client %d] set_sample_rate %u clamped to %u S/s\n"
                            "  -> In SDR++ set sample rate to 2.048MHz or 2.4MHz "
                            "for best audio quality\n",
                            slot, param, applied);
                    else
                        fprintf(stdout, "[client %d] set_sample_rate %u S/s\n",
                                slot, applied);
                    g_dev.sample_rate = applied;
                    msisdr_set_sample_rate(g_dev.dev, applied);
                }
                break;
            case 0x03: /* SET_GAIN_MODE */
                g_dev.gain_mode = (int)param;
                if (g_dev.dev) msisdr_set_tuner_gain_mode(g_dev.dev, g_dev.gain_mode);
                break;
            case 0x04: /* SET_GAIN (tenths of dB) */
                g_dev.gain = (int)(param / 10);
                fprintf(stdout, "[client %d] set_gain %.1f dB\n", slot, param / 10.0);
                if (g_dev.dev && g_dev.gain_mode == 1)
                    msisdr_set_tuner_gain(g_dev.dev, g_dev.gain);
                break;
            case 0x08: /* SET_AGC */
                g_dev.gain_mode = param ? 0 : 1;
                if (g_dev.dev) msisdr_set_tuner_gain_mode(g_dev.dev, g_dev.gain_mode);
                break;
            default: break;
        }
    }

    /* Client disconnected */
    fprintf(stdout, "client %d disconnected\n", slot);
    pthread_mutex_lock(&g_clients_mtx);
    close(sock);                           /* safe here — we own this sock */
    g_clients[slot].sock = -1;
    g_client_fds[slot]   = -1;            /* signal handler sees -1 now */
    pthread_mutex_unlock(&g_clients_mtx);
    return NULL;
}

/* ── USB streaming thread ────────────────────────────────────────── */
static void *usb_stream_thread(void *arg) {
    (void)arg;
    /* Auto-restart streaming on failure (overflow, endpoint stall, etc).
     * Each call to msisdr_read_async re-initialises the macOS endpoint
     * (clear_halt + streaming_start) so restarts recover cleanly. */
    while (!g_stop) {
        g_dev.dc_avg_i = 0.0f;
        g_dev.dc_avg_q = 0.0f;
        /* msisdr_read_sync_stream: one libusb_bulk_transfer at a time.
         * 512 KB buffer — macOS IOUSBLib aggregates many USB micro-packets
         * into one super-packet; the buffer must be large enough to hold
         * whatever the OS hands us, otherwise LIBUSB_ERROR_OVERFLOW. */
        int r = msisdr_read_sync_stream(g_dev.dev, msisdr_callback, NULL,
                                        524288);
        if (g_stop) break;
        fprintf(stderr, "streaming stopped (r=%d) — restarting in 1s...\n", r);
        sleep(1);
    }
    return NULL;
}

/* ── Signal handler — ONLY async-signal-safe operations ─────────── */
static volatile int g_lsock_for_signal = -1;   /* set in main() before loop */

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
    /* msisdr_cancel_async just writes a volatile int — safe from signal */
    if (g_dev.dev) msisdr_cancel_async(g_dev.dev);

    /* shutdown() is async-signal-safe; close() is not.
     * Calling shutdown() unblocks recv() in command threads and accept()
     * in the main thread WITHOUT touching the FD table (no close/reuse risk).
     * The actual close() happens in the respective threads after they unblock.
     *
     * pthread_mutex_lock() is NOT async-signal-safe — if the callback holds
     * g_clients_mtx when the signal arrives, calling it here deadlocks.
     * We use g_client_fds[] (written under mutex, read lock-free here) to
     * safely read the FDs we need to shutdown. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = g_client_fds[i];
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
    }
    int lsock = g_lsock_for_signal;
    if (lsock >= 0) {
        g_lsock_for_signal = -1;
        close(lsock);   /* close() is async-signal-safe; guarantees accept()
                         * returns EBADF immediately even if EINTR is missed */
    }
}

/* ── Main ───────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int      port    = DEFAULT_PORT;
    int      dev_idx = DEFAULT_DEVICE_INDEX;
    g_dev.gain       = DEFAULT_GAIN;
    g_dev.sample_rate = DEFAULT_SAMPLE_RATE;
    g_dev.center_freq = DEFAULT_CENTER_FREQ;
    g_dev.gain_mode  = 1;
    g_dev.dc_avg_i   = 0.0f;
    g_dev.dc_avg_q   = 0.0f;
    g_dev.dev        = NULL;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_clients[i].sock = -1; g_clients[i].tid = 0; g_client_fds[i] = -1;
    }

    int opt;
    while ((opt = getopt(argc, argv, "p:d:g:r:f:h")) != -1) {
        switch (opt) {
            case 'p': port           = atoi(optarg); break;
            case 'd': dev_idx        = atoi(optarg); break;
            case 'g': g_dev.gain     = atoi(optarg); break;
            case 'r': g_dev.sample_rate = (uint32_t)atoi(optarg); break;
            case 'f': g_dev.center_freq = (uint32_t)atoi(optarg); break;
            default:
                fprintf(stderr,
                    "Usage: %s [-p port] [-d device] [-g gain_dB] "
                    "[-r sample_rate] [-f center_freq_Hz]\n",
                    argv[0]);
                return 1;
        }
    }

    /* Use sigaction (not signal) so SA_RESTART is NOT set.
     * With signal() on macOS, blocked syscalls (accept, recv) are
     * automatically restarted after the handler returns — the process
     * never wakes up from accept() and Ctrl+C appears to do nothing. */
    {
        struct sigaction sa;
        sa.sa_handler = on_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;          /* no SA_RESTART — let syscalls return EINTR */
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, NULL);
    }

    /* Listen socket */
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(lsock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons((uint16_t)port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(lsock, MAX_CLIENTS) < 0) { perror("listen"); return 1; }
    g_lsock_for_signal = lsock;   /* signal handler can shutdown this */

    /* Local IP — prefer physical LAN interfaces (en*), skip loopback,
     * virtual (vmenet/bridge/utun/awdl) and Parallels (192.168.64/105.x). */
    char local_ip[64] = "";
    FILE *f = popen(
        "ifconfig 2>/dev/null | awk '"
        "  /^en[0-9]/{iface=$1} "
        "  /inet / && iface ~ /^en/ {"
        "    ip=$2;"
        "    if (ip !~ /^127/ && ip !~ /^192\\.168\\.(64|105)\\./) print ip"
        "  }'"
        " | head -1", "r");
    if (f) {
        fgets(local_ip, sizeof(local_ip), f); pclose(f);
        local_ip[strcspn(local_ip, "\n")] = '\0';
    }
    if (!local_ip[0]) strcpy(local_ip, "(run 'ifconfig' to find your IP)");

    fprintf(stdout,
        "============================================\n"
        "  rtl_tcp server for MSi SDR  (up to %d clients)\n"
        "============================================\n"
        "  Host : %s\n"
        "  Port : %d\n"
        "\n"
        "  SDR++ : Source -> RTL-TCP -> %s:%d\n"
        "  sdrtouch: Start -> %s:%d\n"
        "============================================\n"
        "  rate=%u S/s  gain=%d dB  device=%d\n"
        "  Press Ctrl+C to stop.\n\n",
        MAX_CLIENTS,
        local_ip, port, local_ip, port, local_ip, port,
        g_dev.sample_rate, g_dev.gain, dev_idx);

    /* Open device with retry */
    {
        int opened = 0;
        for (int attempt = 0; attempt < 5 && !g_stop; attempt++) {
            if (msisdr_open(&g_dev.dev, (uint32_t)dev_idx) == 0) {
                opened = 1; break;
            }
            if (attempt == 0)
                fprintf(stderr,
                    "\n*** MSi SDR device not found — is the dongle plugged in? ***\n"
                    "    Retrying...\n\n");
            sleep(1);
        }
        if (!opened) {
            fprintf(stderr,
                "ERROR: MSi SDR device %d not found.\n"
                "  Plug the dongle into a Mac USB port and re-run.\n", dev_idx);
            close(lsock);
            return 1;
        }
    }

    /* Configure device */
    msisdr_rsp1a_gpio_init(g_dev.dev);
    msisdr_set_hw_flavour(g_dev.dev, MSISDR_HW_DEFAULT);
    msisdr_set_sample_format(g_dev.dev, "AUTO");
    msisdr_set_if_freq(g_dev.dev, 0);
    msisdr_set_bandwidth(g_dev.dev, 8000000);
    msisdr_set_sample_rate(g_dev.dev, g_dev.sample_rate);
    msisdr_set_center_freq(g_dev.dev, g_dev.center_freq);
    msisdr_set_tuner_gain_mode(g_dev.dev, 1);
    msisdr_set_tuner_gain(g_dev.dev, g_dev.gain);
    msisdr_reset_buffer(g_dev.dev);
    fprintf(stdout, "MSi SDR opened — streaming: %u Hz, gain %d dB, %u S/s\n\n",
            g_dev.center_freq, g_dev.gain, g_dev.sample_rate);

    /* Start USB streaming thread */
    pthread_t usb_tid;
    pthread_create(&usb_tid, NULL, usb_stream_thread, NULL);

    /* Accept clients */
    while (!g_stop) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int csock = accept(lsock, (struct sockaddr *)&caddr, &clen);
        if (csock < 0) {
            /* EINTR: interrupted by signal (no SA_RESTART).
             * EBADF/EINVAL: lsock was closed by signal handler. */
            if (g_stop || errno == EINTR || errno == EBADF || errno == EINVAL)
                break;
            perror("accept"); continue;
        }

        /* Find free slot */
        pthread_mutex_lock(&g_clients_mtx);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].sock < 0) { slot = i; break; }
        }

        if (slot < 0) {
            pthread_mutex_unlock(&g_clients_mtx);
            fprintf(stderr, "max %d clients already connected — rejecting\n",
                    MAX_CLIENTS);
            close(csock);
            continue;
        }

        /* Send RTL0 magic header */
        uint8_t header[12] = {
            'R','T','L','0',
            0,0,0,TUNER_TYPE,
            0,0,0,GAIN_STEPS
        };
        if (write(csock, header, sizeof(header)) != sizeof(header)) {
            pthread_mutex_unlock(&g_clients_mtx);
            close(csock);
            continue;
        }

        g_clients[slot].sock = csock;
        g_client_fds[slot]   = csock;     /* signal handler can shutdown this */
        pthread_mutex_unlock(&g_clients_mtx);

        fprintf(stdout, "client %d connected from %s\n",
                slot, inet_ntoa(caddr.sin_addr));

        /* Start per-client command thread — NOT detached so we can join it
         * at shutdown.  Detached threads survive main() exit, causing the
         * "process still running after Ctrl+C" symptom. */
        cmd_thread_arg_t *a = malloc(sizeof(*a));
        a->slot = slot;
        a->sock = csock;
        pthread_create(&g_clients[slot].tid, NULL, client_cmd_thread, a);
    }

    /* Shutdown — join all threads so the process exits completely */
    pthread_join(usb_tid, NULL);

    /* Join any still-running command threads (sockets already closed by
     * the signal handler, so their recv() unblocked and they exit fast). */
    pthread_mutex_lock(&g_clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_t tid = g_clients[i].tid;
        pthread_mutex_unlock(&g_clients_mtx);
        if (tid) pthread_join(tid, NULL);
        pthread_mutex_lock(&g_clients_mtx);
        g_clients[i].tid = 0;
    }
    pthread_mutex_unlock(&g_clients_mtx);

    msisdr_close(g_dev.dev);
    /* lsock was already closed by the signal handler */
    fprintf(stdout, "server stopped\n");
    return 0;
}
