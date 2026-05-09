#pragma once
#include <stddef.h>

/* Maximum log file size before rotation (default 5 MB) */
#define LOG_MAX_BYTES_DEFAULT (5 * 1024 * 1024)

/**
 * Initialise the logger.
 *
 * Opens the system logger (syslog on Linux, os_log-compatible on macOS)
 * and a rotating local file.  When the file exceeds max_bytes it is renamed
 * to <path>.old and a new file is started.
 *
 * @param path      Path to the log file (NULL = no file logging)
 * @param max_bytes Rotate when file exceeds this size
 */
void log_init(const char *path, size_t max_bytes);

/** Close the system logger and the log file. */
void log_close(void);

/** Log an informational message (syslog LOG_INFO + file). */
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Log an error message (syslog LOG_ERR + file). */
void log_err(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));
