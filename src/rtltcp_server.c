/*
 * rtltcp_server.c — rtl_tcp-compatible TCP server for MSi SDR hardware.
 *
 * Implements the standard rtl_tcp wire protocol so any rtl_tcp client
 * (SDR++, sdrtouch, HDSDR, SDR#, gqrx …) can connect over the network
 * and receive raw IQ samples from an MSi2500/MSi001 USB dongle.
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
 *   0x05  SET_PPM         (ignored — MSi2500 has no correction register)
 *   0x08  SET_AGC         0=off, 1=on (maps to gain_mode=0)
 *
 * Usage: rtltcp_server [-p port] [-d device_index] [-g gain_dB] [-r sample_rate]
 *
 * Build: added to CMakeLists.txt as target "rtltcp_server".
 */

#include <arpa/inet.h>
#include <errno.h>
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

/* ── Configuration defaults ──────────────────────────────────────── */
#define DEFAULT_PORT         1234
#define DEFAULT_DEVICE_INDEX 0
#define DEFAULT_GAIN         40       /* dB, 0..102 */
#define DEFAULT_SAMPLE_RATE  2048000
#define DEFAULT_CENTER_FREQ  100000000

/* rtl_tcp tuner type sent in the magic header.
 * 5 = RTLSDR_TUNER_R820T (most clients accept any non-zero value). */
#define TUNER_TYPE           5
#define GAIN_STEPS           29       /* number of gain steps advertised */

/* IQ buffer: number of samples to accumulate before writing to socket.
 * libmsisdr's async callback is called with however many bytes we request. */
#define SEND_BUF_SAMPLES     16384

/* ── Shared state between the USB callback and the send thread ───── */
typedef struct {
    msisdr_dev_t *dev;
    int           client_sock;
    volatile int  running;

    /* Current RF settings (updated by command thread) */
    volatile uint32_t center_freq;
    volatile uint32_t sample_rate;
    volatile int      gain_mode;   /* 0=auto, 1=manual */
    volatile int      gain;        /* dB */

    pthread_mutex_t   mutex;
} server_state_t;

static server_state_t g_state;
static volatile int   g_stop = 0;

/* ── rtl_tcp command structure ───────────────────────────────────── */
typedef struct { uint8_t cmd; uint32_t param; } __attribute__((packed)) rtltcp_cmd_t;

/* ── IQ conversion: 16-bit signed → 8-bit unsigned ─────────────── */
static void convert_s16_to_u8(const int16_t *in, uint8_t *out, uint32_t n_samples) {
    for (uint32_t i = 0; i < n_samples; i++) {
        out[i] = (uint8_t)((in[i] >> 8) + 128);
    }
}

/* ── MSi SDR async callback ─────────────────────────────────────── */
static void msisdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
    server_state_t *state = (server_state_t *)ctx;
    if (!state->running || state->client_sock < 0) return;

    /* buf is int16_t I/Q pairs from libmsisdr's format converters.
     * len is in bytes; each sample is 2 bytes (int16_t). */
    uint32_t n_samples = len / sizeof(int16_t);
    uint8_t *u8buf = malloc(n_samples);
    if (!u8buf) return;

    convert_s16_to_u8((const int16_t *)buf, u8buf, n_samples);

    /* Write to client socket. Ignore EPIPE — client disconnected. */
    ssize_t sent = 0;
    while (sent < (ssize_t)n_samples) {
        ssize_t r = write(state->client_sock,
                          u8buf + sent, n_samples - sent);
        if (r <= 0) {
            state->running = 0;
            break;
        }
        sent += r;
    }
    free(u8buf);
}

/* ── Command listener thread (client → server) ───────────────────── */
static void apply_settings(server_state_t *state) {
    if (!state->dev) return;
    msisdr_set_center_freq(state->dev, state->center_freq);
    msisdr_set_tuner_gain_mode(state->dev, state->gain_mode);
    if (state->gain_mode == 1) {
        msisdr_set_tuner_gain(state->dev, state->gain);
    }
}

static void *cmd_thread_fn(void *arg) {
    server_state_t *state = (server_state_t *)arg;
    rtltcp_cmd_t cmd;

    while (state->running) {
        ssize_t n = recv(state->client_sock, &cmd, sizeof(cmd), MSG_WAITALL);
        if (n <= 0) break;

        uint32_t param = ntohl(cmd.param);
        switch (cmd.cmd) {
            case 0x01: /* SET_FREQ */
                state->center_freq = param;
                fprintf(stdout, "cmd: set_freq %u Hz\n", param);
                if (state->dev) msisdr_set_center_freq(state->dev, param);
                break;
            case 0x02: /* SET_SAMPLE_RATE */
                fprintf(stdout, "cmd: set_sample_rate %u S/s (ignored mid-stream)\n", param);
                break;
            case 0x03: /* SET_GAIN_MODE */
                state->gain_mode = (int)param;
                fprintf(stdout, "cmd: set_gain_mode %u\n", param);
                if (state->dev) msisdr_set_tuner_gain_mode(state->dev, state->gain_mode);
                break;
            case 0x04: /* SET_GAIN  (tenths of dB) */
                state->gain = (int)(param / 10);
                fprintf(stdout, "cmd: set_gain %u (%.1f dB)\n", param, param / 10.0);
                if (state->dev && state->gain_mode == 1)
                    msisdr_set_tuner_gain(state->dev, state->gain);
                break;
            case 0x08: /* SET_AGC_MODE */
                state->gain_mode = param ? 0 : 1; /* AGC on = auto */
                if (state->dev) msisdr_set_tuner_gain_mode(state->dev, state->gain_mode);
                break;
            default:
                break; /* silently ignore unknown commands */
        }
    }
    state->running = 0;
    return NULL;
}

/* ── Serve one connected client ─────────────────────────────────── */
static void serve_client(int csock, int device_index,
                          uint32_t freq, uint32_t rate, int gain) {
    g_state.client_sock = csock;
    g_state.running     = 1;
    g_state.center_freq = freq;
    g_state.sample_rate = rate;
    g_state.gain_mode   = 1;
    g_state.gain        = gain;
    g_state.dev         = NULL;

    /* ── Send rtl_tcp magic header ── */
    uint8_t header[12] = {
        'R','T','L','0',
        0,0,0,TUNER_TYPE,                /* tuner_type big-endian */
        0,0,0,GAIN_STEPS                 /* gain_count big-endian */
    };
    if (write(csock, header, sizeof(header)) != sizeof(header)) goto done;
    fprintf(stdout, "sent rtl_tcp header to client\n");

    /* ── Open MSi SDR ── */
    if (msisdr_open(&g_state.dev, (uint32_t)device_index) < 0) {
        fprintf(stderr, "<3>failed to open MSi SDR device %d\n", device_index);
        goto done;
    }
    fprintf(stdout, "MSi SDR opened (device %d)\n", device_index);

    msisdr_set_hw_flavour(g_state.dev, MSISDR_HW_DEFAULT);
    msisdr_set_sample_format(g_state.dev, "504_S16");
    msisdr_set_if_freq(g_state.dev, 0);
    msisdr_set_bandwidth(g_state.dev, 8000000);

    if (msisdr_set_sample_rate(g_state.dev, rate) < 0) {
        fprintf(stderr, "<3>failed to set sample rate %u\n", rate);
        goto close_dev;
    }
    if (msisdr_set_center_freq(g_state.dev, freq) < 0) {
        fprintf(stderr, "<3>failed to set center freq %u Hz\n", freq);
        goto close_dev;
    }
    msisdr_set_tuner_gain_mode(g_state.dev, 1);
    msisdr_set_tuner_gain(g_state.dev, gain);
    msisdr_reset_buffer(g_state.dev);

    fprintf(stdout, "streaming: %u Hz, gain %d dB, rate %u S/s\n",
            freq, gain, rate);

    /* ── Start command listener thread ── */
    pthread_t ctid;
    pthread_create(&ctid, NULL, cmd_thread_fn, &g_state);

    /* ── Block in USB read loop until client disconnects ── */
    msisdr_read_async(g_state.dev, msisdr_callback, &g_state, 0, SEND_BUF_SAMPLES * 2);

    g_state.running = 0;
    msisdr_cancel_async(g_state.dev);
    pthread_join(ctid, NULL);
    fprintf(stdout, "client disconnected\n");

close_dev:
    msisdr_close(g_state.dev);
    g_state.dev = NULL;
done:
    close(csock);
}

/* ── Signal handler ─────────────────────────────────────────────── */
static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
    g_state.running = 0;
    if (g_state.dev) msisdr_cancel_async(g_state.dev);
}

/* ── Main ───────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int      port    = DEFAULT_PORT;
    int      dev_idx = DEFAULT_DEVICE_INDEX;
    int      gain    = DEFAULT_GAIN;
    uint32_t rate    = DEFAULT_SAMPLE_RATE;
    uint32_t freq    = DEFAULT_CENTER_FREQ;

    int opt;
    while ((opt = getopt(argc, argv, "p:d:g:r:f:h")) != -1) {
        switch (opt) {
            case 'p': port    = atoi(optarg); break;
            case 'd': dev_idx = atoi(optarg); break;
            case 'g': gain    = atoi(optarg); break;
            case 'r': rate    = (uint32_t)atoi(optarg); break;
            case 'f': freq    = (uint32_t)atoi(optarg); break;
            default:
                fprintf(stderr,
                    "Usage: %s [-p port] [-d device] [-g gain_dB] "
                    "[-r sample_rate] [-f center_freq_Hz]\n"
                    "  Defaults: port=%d device=0 gain=%d "
                    "rate=%u freq=%u\n",
                    argv[0], DEFAULT_PORT, DEFAULT_GAIN,
                    DEFAULT_SAMPLE_RATE, DEFAULT_CENTER_FREQ);
                return 1;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    /* Create listen socket */
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons((uint16_t)port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(lsock, 1) < 0) { perror("listen"); return 1; }

    /* Get local IP for display */
    char local_ip[64] = "0.0.0.0";
    FILE *f = popen("ipconfig getifaddr en0 2>/dev/null || ifconfig en0 | awk '/inet /{print $2}'", "r");
    if (f) { fgets(local_ip, sizeof(local_ip), f); pclose(f);
             local_ip[strcspn(local_ip, "\n")] = '\0'; }

    fprintf(stdout,
        "============================================\n"
        "  rtl_tcp server for MSi SDR\n"
        "============================================\n"
        "  Connect from phone / SDR app on same WiFi:\n"
        "\n"
        "    Host : %s\n"
        "    Port : %d\n"
        "\n"
        "  In SDR++ : Source -> RTL-TCP -> %s:%d\n"
        "  In sdrtouch: Start -> %s:%d\n"
        "============================================\n"
        "  rate=%u S/s  gain=%d dB  device=%d\n"
        "  Waiting for client...\n\n",
        local_ip, port, local_ip, port, local_ip, port,
        rate, gain, dev_idx);

    while (!g_stop) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int csock = accept(lsock, (struct sockaddr *)&caddr, &clen);
        if (csock < 0) {
            if (g_stop) break;
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        fprintf(stdout, "client connected from %s\n",
                inet_ntoa(caddr.sin_addr));
        serve_client(csock, dev_idx, freq, rate, gain);
        fprintf(stdout, "waiting for next client...\n\n");
    }

    close(lsock);
    fprintf(stdout, "server stopped\n");
    return 0;
}
