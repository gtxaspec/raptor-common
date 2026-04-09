#include "greatest.h"
#include "rss_http.h"

#include <string.h>

/* rss_base64_init() called once in suite setup */

/* ------------------------------------------------------------------ */
/* rss_base64_decode                                                   */
/* ------------------------------------------------------------------ */

TEST base64_decode_basic(void)
{
	char out[64];
	int len = rss_base64_decode("SGVsbG8=", 8, out, sizeof(out));
	ASSERT_EQ(5, len);
	ASSERT_STR_EQ("Hello", out);
	PASS();
}

TEST base64_decode_padding(void)
{
	char out[64];
	/* "a" = "YQ==" */
	int len = rss_base64_decode("YQ==", 4, out, sizeof(out));
	ASSERT_EQ(1, len);
	ASSERT_EQ('a', out[0]);
	/* "ab" = "YWI=" */
	len = rss_base64_decode("YWI=", 4, out, sizeof(out));
	ASSERT_EQ(2, len);
	ASSERT_EQ('a', out[0]);
	ASSERT_EQ('b', out[1]);
	PASS();
}

TEST base64_decode_no_padding(void)
{
	char out[64];
	/* "Hello" without trailing = */
	int len = rss_base64_decode("SGVsbG8", 7, out, sizeof(out));
	ASSERT_EQ(5, len);
	ASSERT_STR_EQ("Hello", out);
	PASS();
}

TEST base64_decode_empty(void)
{
	char out[8] = "x";
	int len = rss_base64_decode("", 0, out, sizeof(out));
	ASSERT_EQ(0, len);
	PASS();
}

TEST base64_decode_overflow(void)
{
	char out[2];
	/* "Hello" decoded is 5 bytes, out_max is 2 */
	int len = rss_base64_decode("SGVsbG8=", 8, out, 2);
	ASSERT_EQ(-1, len);
	PASS();
}

/* ------------------------------------------------------------------ */
/* rss_http_check_basic_auth                                           */
/* ------------------------------------------------------------------ */

TEST http_basic_auth_valid(void)
{
	/* "user:pass" = "dXNlcjpwYXNz" */
	const char *req = "GET / HTTP/1.1\r\nAuthorization: Basic dXNlcjpwYXNz\r\n\r\n";
	ASSERT(rss_http_check_basic_auth(req, "user", "pass"));
	PASS();
}

TEST http_basic_auth_wrong_pass(void)
{
	const char *req = "GET / HTTP/1.1\r\nAuthorization: Basic dXNlcjpwYXNz\r\n\r\n";
	ASSERT_FALSE(rss_http_check_basic_auth(req, "user", "wrong"));
	PASS();
}

TEST http_basic_auth_no_header(void)
{
	const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	ASSERT_FALSE(rss_http_check_basic_auth(req, "user", "pass"));
	PASS();
}

TEST http_basic_auth_empty_user(void)
{
	const char *req = "GET / HTTP/1.1\r\n\r\n";
	ASSERT(rss_http_check_basic_auth(req, "", "anything"));
	PASS();
}

TEST http_basic_auth_malformed(void)
{
	/* Invalid base64 that can't decode to user:pass */
	const char *req = "GET / HTTP/1.1\r\nAuthorization: Basic !!!!\r\n\r\n";
	ASSERT_FALSE(rss_http_check_basic_auth(req, "user", "pass"));
	PASS();
}

static void http_setup(void *arg)
{
	(void)arg;
	rss_base64_init();
}

SUITE(http_suite)
{
	SET_SETUP(http_setup, NULL);
	RUN_TEST(base64_decode_basic);
	RUN_TEST(base64_decode_padding);
	RUN_TEST(base64_decode_no_padding);
	RUN_TEST(base64_decode_empty);
	RUN_TEST(base64_decode_overflow);
	RUN_TEST(http_basic_auth_valid);
	RUN_TEST(http_basic_auth_wrong_pass);
	RUN_TEST(http_basic_auth_no_header);
	RUN_TEST(http_basic_auth_empty_user);
	RUN_TEST(http_basic_auth_malformed);
}
