/*
 * rss_http.c -- Shared HTTP helpers
 */

#include "rss_http.h"
#include "rss_common.h"

#include <string.h>
#include <strings.h> /* strncasecmp */
#include <stdint.h>

/* Base64 decode table — self-initializing on first use.
 * 0xFF = invalid character (skipped by decoder). */
static uint8_t b64_table[256];
static bool b64_ready;

static void b64_ensure_init(void)
{
    if (b64_ready)
        return;
    static const char b64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    memset(b64_table, 0xFF, sizeof(b64_table));
    for (int i = 0; b64_chars[i]; i++)
        b64_table[(uint8_t)b64_chars[i]] = (uint8_t)i;
    b64_ready = true;
}

void rss_base64_init(void) { b64_ensure_init(); }

int rss_base64_decode(const char *in, size_t in_len, char *out, size_t out_max)
{
    b64_ensure_init();
    size_t out_len = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len && in[i] != '='; i++) {
        uint8_t v = b64_table[(uint8_t)in[i]];
        if (v == 0xFF)
            continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len >= out_max)
                return -1;
            out[out_len++] = (char)(buf >> bits);
        }
    }
    if (out_len < out_max)
        out[out_len] = '\0';
    return (int)out_len;
}

bool rss_http_check_basic_auth(const char *request, const char *user, const char *pass)
{
    if (!request || !user || !pass)
        return false;

    /* SECURITY: empty username = auth disabled. Intentional for embedded
     * deployments that don't configure credentials. Callers should warn
     * at startup if http_user is empty and the endpoint is network-exposed. */
    if (!user[0])
        return true;

    /* Case-insensitive header name match per RFC 7230 */
    static const char needle[] = "Authorization: Basic ";
    size_t needle_len = sizeof(needle) - 1;
    const char *auth = NULL;
    const char *p = request;
    while (*p) {
        if ((p == request || *(p - 1) == '\n') &&
            strncasecmp(p, needle, needle_len) == 0) {
            auth = p + needle_len;
            break;
        }
        p++;
    }
    if (!auth)
        return false;

    const char *end = auth;
    while (*end && *end != '\r' && *end != '\n' && *end != ' ')
        end++;

    /* Credentials longer than ~190 chars (pre-base64) will be truncated */
    char decoded[256];
    int dlen = rss_base64_decode(auth, (size_t)(end - auth), decoded, sizeof(decoded) - 1);
    if (dlen <= 0)
        return false;
    decoded[dlen] = '\0';

    char *colon = strchr(decoded, ':');
    if (!colon)
        return false;
    *colon = '\0';

    /* Evaluate both comparisons to prevent timing side-channel that
     * leaks whether the username is valid (short-circuit && would skip
     * the password comparison on username mismatch). */
    bool user_ok = rss_secure_compare(decoded, user);
    bool pass_ok = rss_secure_compare(colon + 1, pass);
    return user_ok & pass_ok;
}
