/*
 * rss_http.h -- Shared HTTP helpers for raptor daemons
 *
 * Base64 decode, HTTP Basic auth, common response helpers.
 * Used by RHD (HTTP snapshots/MJPEG) and RWD (WHIP signaling).
 */

#ifndef RSS_HTTP_H
#define RSS_HTTP_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Initialize the base64 decode table. Call once at startup.
 */
void rss_base64_init(void);

/*
 * Decode base64 string. Returns decoded length, -1 on error.
 * Output is null-terminated if space permits.
 */
int rss_base64_decode(const char *in, size_t in_len, char *out, size_t out_max);

/*
 * Check HTTP Basic auth in a request buffer.
 * Returns true if authorized (or if user is empty = no auth configured).
 * Uses constant-time comparison to prevent timing attacks.
 */
bool rss_http_check_basic_auth(const char *request, const char *user, const char *pass);

#endif /* RSS_HTTP_H */
