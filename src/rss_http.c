/*
 * rss_http.c -- Shared HTTP helpers
 */

#include "rss_http.h"
#include "rss_common.h"

#include <string.h>
#include <stdint.h>

/* Base64 decode table — built once by rss_base64_init() */
static uint8_t b64_table[256];

void rss_base64_init(void)
{
	static const char b64_chars[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	memset(b64_table, 0xFF, sizeof(b64_table));
	for (int i = 0; b64_chars[i]; i++)
		b64_table[(uint8_t)b64_chars[i]] = (uint8_t)i;
}

int rss_base64_decode(const char *in, size_t in_len, char *out, size_t out_max)
{
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
	if (!user[0])
		return true;

	const char *needle = "Authorization: Basic ";
	const char *auth = NULL;
	const char *p = request;
	while ((p = strstr(p, needle)) != NULL) {
		if (p == request || *(p - 1) == '\n') {
			auth = p + strlen(needle);
			break;
		}
		p++;
	}
	if (!auth)
		return false;

	const char *end = auth;
	while (*end && *end != '\r' && *end != '\n' && *end != ' ')
		end++;

	char decoded[256];
	int dlen = rss_base64_decode(auth, (size_t)(end - auth), decoded, sizeof(decoded) - 1);
	if (dlen <= 0)
		return false;
	decoded[dlen] = '\0';

	char *colon = strchr(decoded, ':');
	if (!colon)
		return false;
	*colon = '\0';

	return rss_secure_compare(decoded, user) &&
	       rss_secure_compare(colon + 1, pass);
}
