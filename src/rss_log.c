/*
 * rss_log.c — Raptor Streaming System logging
 *
 * Thread-safe logging to stderr, syslog, or file with millisecond
 * timestamps and configurable verbosity levels.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static char s_daemon_name[64];
static rss_log_level_t s_level = RSS_LOG_INFO;
static rss_log_target_t s_target = RSS_LOG_TARGET_STDERR;
static FILE *s_fp = NULL; /* file target */

static const char *level_names[] = {
    "FATAL", "ERROR", "WARN ", "INFO ", "DEBUG", "TRACE",
};

/* Map RSS log levels to syslog priorities */
static const int syslog_prio[] = {
    LOG_CRIT,    /* FATAL */
    LOG_ERR,     /* ERROR */
    LOG_WARNING, /* WARN  */
    LOG_INFO,    /* INFO  */
    LOG_DEBUG,   /* DEBUG */
    LOG_DEBUG,   /* TRACE */
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void rss_log_init(const char *daemon_name, rss_log_level_t level, rss_log_target_t target,
                  const char *log_file)
{
    rss_strlcpy(s_daemon_name, daemon_name ? daemon_name : "rss", (int)sizeof(s_daemon_name));
    s_level = level;
    s_target = target;

    if (target == RSS_LOG_TARGET_SYSLOG) {
        openlog(s_daemon_name, LOG_PID | LOG_NDELAY, LOG_DAEMON);
    } else if (target == RSS_LOG_TARGET_FILE && log_file) {
        s_fp = fopen(log_file, "a");
        if (!s_fp) {
            /* Fall back to stderr if we can't open the log file */
            s_target = RSS_LOG_TARGET_STDERR;
            fprintf(stderr, "%s: failed to open log file %s, using stderr\n", s_daemon_name,
                    log_file);
        }
    }
}

void rss_log_set_level(rss_log_level_t level)
{
    s_level = level;
}

rss_log_level_t rss_log_get_level(void)
{
    return s_level;
}

void rss_vlog(rss_log_level_t level, const char *file, int line, const char *fmt, va_list ap)
{
    if (level > s_level)
        return;

    if (s_target == RSS_LOG_TARGET_SYSLOG) {
        char msg[512];
        vsnprintf(msg, sizeof(msg), fmt, ap);
        syslog(syslog_prio[level], "[%s] %s:%d: %s", level_names[level], file, line, msg);
        return;
    }

    /* Build timestamp HH:MM:SS.mmm from wall clock */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char tsbuf[16];
    int ms = (int)(ts.tv_nsec / 1000000);
    snprintf(tsbuf, sizeof(tsbuf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);

    FILE *out = (s_target == RSS_LOG_TARGET_FILE && s_fp) ? s_fp : stderr;

    flockfile(out);

    fprintf(out, "%s %s [%s] %s:%d: ", tsbuf, s_daemon_name, level_names[level], file, line);
    vfprintf(out, fmt, ap);

    /* Ensure newline */
    int len = (int)strlen(fmt);
    if (len == 0 || fmt[len - 1] != '\n')
        fputc('\n', out);

    fflush(out);
    funlockfile(out);
}

void rss_log(rss_log_level_t level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    rss_vlog(level, file, line, fmt, ap);
    va_end(ap);
}
