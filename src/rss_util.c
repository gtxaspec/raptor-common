/*
 * rss_util.c — Raptor Streaming System utilities
 *
 * Timestamps, string helpers, and file I/O for embedded use.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

/* ================================================================
 * Timestamps
 * ================================================================ */

int64_t rss_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int64_t rss_wallclock_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

char *rss_format_timestamp(char *buf, int buf_size)
{
    return rss_format_timestamp_fmt(buf, buf_size, "%Y-%m-%d %H:%M:%S");
}

char *rss_format_timestamp_fmt(char *buf, int buf_size, const char *fmt)
{
    if (!buf || buf_size < 1)
        return buf;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    strftime(buf, (size_t)buf_size, fmt, &tm);
    return buf;
}

/* ================================================================
 * String Utilities
 * ================================================================ */

size_t rss_strlcpy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size < 1)
        return 0;

    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    size_t i;
    for (i = 0; i < dst_size - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';

    /* Return total src length (like BSD strlcpy) for truncation detection:
     * if return value >= dst_size, truncation occurred. */
    size_t src_len = i;
    while (src[src_len] != '\0')
        src_len++;
    return src_len;
}

char *rss_trim(char *s)
{
    if (!s)
        return s;

    /* Skip leading whitespace */
    while (*s && isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    /* Find end, trim trailing whitespace */
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';

    return s;
}

bool rss_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return false;
    size_t plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

bool rss_secure_compare(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    /* Always compare alen bytes to avoid leaking length via early exit.
     * XOR accumulator is non-zero on any mismatch. */
    volatile unsigned char diff = (unsigned char)(alen ^ blen);
    for (size_t i = 0; i < alen; i++)
        diff |= (unsigned char)a[i] ^ (unsigned char)(i < blen ? b[i] : 0);
    return diff == 0;
}

/* ================================================================
 * JSON Helpers
 * ================================================================ */

int rss_json_get_str(const char *json, const char *key, char *buf, int buf_size)
{
    if (!json || !key || !buf || buf_size < 1)
        return -1;
    if (strlen(key) > 54) /* pattern: "key":"  must fit in 64 bytes */
        return -1;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return -1;
    p += strlen(pattern);
    const char *end = strchr(p, '"');
    if (!end || end < p)
        return -1;
    int len = (int)(end - p);
    if (len >= buf_size)
        len = buf_size - 1;
    memcpy(buf, p, (size_t)len);
    buf[len] = '\0';
    return 0;
}

int rss_json_get_int(const char *json, const char *key, int *out)
{
    if (!json || !key || !out)
        return -1;
    if (strlen(key) > 56) /* pattern: "key":  must fit in 64 bytes */
        return -1;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return -1;
    p += strlen(pattern);
    while (*p == ' ')
        p++;
    char *end;
    long val = strtol(p, &end, 10);
    if (end == p)
        return -1;
    if (val > INT_MAX || val < INT_MIN)
        return -1;
    *out = (int)val;
    return 0;
}

/* ================================================================
 * File Utilities
 * ================================================================ */

char *rss_read_file(const char *path, int *out_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }

    /* Use stat size if available, otherwise default to 4KB (proc files report 0) */
    int capacity = (st.st_size > 0) ? (int)st.st_size + 1 : 4096;
    char *buf = malloc((size_t)capacity);
    if (!buf) {
        close(fd);
        return NULL;
    }

    int total = 0;
    for (;;) {
        if (total >= capacity - 1) {
            capacity *= 2;
            char *newbuf = realloc(buf, (size_t)capacity);
            if (!newbuf) {
                free(buf);
                close(fd);
                return NULL;
            }
            buf = newbuf;
        }
        ssize_t n = read(fd, buf + total, (size_t)(capacity - 1 - total));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            return NULL;
        }
        if (n == 0)
            break;
        total += (int)n;
    }
    close(fd);

    buf[total] = '\0';
    if (out_size)
        *out_size = total;
    return buf;
}

int rss_write_file_atomic(const char *path, const void *data, int size)
{
    /* Build temporary path: path + ".tmp" */
    size_t plen = strlen(path);
    char *tmp_path = malloc(plen + 5);
    if (!tmp_path)
        return -1;
    memcpy(tmp_path, path, plen);
    memcpy(tmp_path + plen, ".tmp", 5); /* includes NUL */

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(tmp_path);
        return -1;
    }

    const char *p = (const char *)data;
    int remaining = size;
    while (remaining > 0) {
        ssize_t n = write(fd, p, (size_t)remaining);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            unlink(tmp_path);
            free(tmp_path);
            return -1;
        }
        p += n;
        remaining -= (int)n;
    }

    if (fsync(fd) < 0) {
        close(fd);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }
    close(fd);

    if (rename(tmp_path, path) < 0) {
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int rss_mkdir_p(const char *path)
{
    if (!path || *path == '\0')
        return -1;

    /* Work on a mutable copy */
    size_t len = strlen(path);
    char *tmp = malloc(len + 1);
    if (!tmp)
        return -1;
    memcpy(tmp, path, len + 1);

    /* Walk the path, creating directories as needed */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }

    /* Create the final component */
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
        free(tmp);
        return -1;
    }

    free(tmp);
    return 0;
}
