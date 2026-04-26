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
#include "cJSON.h"
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

    /* Reject inputs exceeding buffer size — avoids silent tail truncation */
    if (alen > 255 || blen > 255)
        return false;

    /* Copy into fixed-size zero-initialized buffers so the comparison
     * loop is constant-length and branchless — always iterates 256 bytes
     * regardless of input lengths. Zero-padding means the tail comparison
     * detects length differences without a separate length check. */
    unsigned char buf_a[256] = {0};
    unsigned char buf_b[256] = {0};
    memcpy(buf_a, a, alen);
    memcpy(buf_b, b, blen);

    volatile unsigned int diff = 0;
    for (size_t i = 0; i < 256; i++)
        diff |= buf_a[i] ^ buf_b[i];
    return diff == 0;
}

/* ================================================================
 * JSON Helpers
 * ================================================================ */

int rss_json_get_str(const char *json, const char *key, char *buf, int buf_size)
{
    if (!json || !key || !buf || buf_size < 1)
        return -1;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;

    cJSON *val = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(val) || !val->valuestring) {
        cJSON_Delete(root);
        return -1;
    }

    rss_strlcpy(buf, val->valuestring, (size_t)buf_size);
    cJSON_Delete(root);
    return 0;
}

int rss_json_get_int(const char *json, const char *key, int *out)
{
    if (!json || !key || !out)
        return -1;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;

    cJSON *val = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(val)) {
        cJSON_Delete(root);
        return -1;
    }

    *out = val->valueint;
    cJSON_Delete(root);
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

    /* Preserve existing file permissions (e.g., 0600 for configs with
     * credentials). Fall back to 0644 for new files. */
    struct stat orig_st;
    mode_t mode = 0644;
    if (stat(path, &orig_st) == 0)
        mode = orig_st.st_mode & 07777;

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, mode);
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

    /* Sync the directory entry so the rename survives a power loss */
    {
        char *dir = strdup(path);
        if (dir) {
            char *slash = strrchr(dir, '/');
            if (slash) {
                *slash = '\0';
                int dfd = open(dir, O_RDONLY);
                if (dfd >= 0) {
                    fsync(dfd);
                    close(dfd);
                }
            }
            free(dir);
        }
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
