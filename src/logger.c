#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

static pthread_mutex_t s_mtx  = PTHREAD_MUTEX_INITIALIZER;
static FILE           *s_file = NULL;
static char            s_path[512];
static size_t          s_max_bytes;
static size_t          s_written;

void log_init(const char *path, size_t max_bytes) {
    openlog("rtltcp_server", LOG_PID | LOG_NDELAY, LOG_USER);
    s_max_bytes = max_bytes ? max_bytes : LOG_MAX_BYTES_DEFAULT;
    s_written   = 0;
    s_path[0]   = '\0';
    if (!path) return;
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_file = fopen(path, "a");
    if (!s_file)
        syslog(LOG_WARNING, "cannot open log file %s: %s", path, strerror(errno));
}

void log_close(void) {
    if (s_file) { fclose(s_file); s_file = NULL; }
    closelog();
}

/* Rotate log file when it exceeds max_bytes.  Called under s_mtx. */
static void maybe_rotate(void) {
    if (!s_file || s_written < s_max_bytes) return;
    fclose(s_file);
    s_file = NULL;

    char old[520];
    snprintf(old, sizeof(old), "%s.old", s_path);
    rename(s_path, old);          /* keep one generation */

    s_file   = fopen(s_path, "w");
    s_written = 0;
    if (!s_file)
        syslog(LOG_WARNING, "log rotation failed for %s: %s", s_path, strerror(errno));
}

static void write_entry(int priority, const char *fmt, va_list ap) {
    /* syslog — OS-standard, visible in Console.app / journalctl */
    va_list ap2;
    va_copy(ap2, ap);
    vsyslog(priority, fmt, ap2);
    va_end(ap2);

    if (!s_file) return;

    /* File: timestamp + level + message */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *level = (priority <= LOG_ERR) ? "ERR " : "INFO";
    int n = fprintf(s_file, "[%s] %s  ", ts, level);
    if (n > 0) s_written += (size_t)n;

    n = vfprintf(s_file, fmt, ap);
    if (n > 0) s_written += (size_t)n;

    fputc('\n', s_file);
    s_written++;

    fflush(s_file);
    maybe_rotate();
}

void log_info(const char *fmt, ...) {
    pthread_mutex_lock(&s_mtx);
    va_list ap; va_start(ap, fmt);
    write_entry(LOG_INFO, fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&s_mtx);
}

void log_err(const char *fmt, ...) {
    pthread_mutex_lock(&s_mtx);
    va_list ap; va_start(ap, fmt);
    write_entry(LOG_ERR, fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&s_mtx);
}
