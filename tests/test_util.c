#include "greatest.h"
#include "rss_common.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* rss_strlcpy                                                         */
/* ------------------------------------------------------------------ */

TEST strlcpy_basic(void)
{
	char buf[10];
	size_t ret = rss_strlcpy(buf, "hello", sizeof(buf));
	ASSERT_EQ(5u, ret); /* src length */
	ASSERT_STR_EQ("hello", buf);
	PASS();
}

TEST strlcpy_truncate(void)
{
	char buf[6];
	size_t ret = rss_strlcpy(buf, "hello world", sizeof(buf));
	ASSERT_EQ(11u, ret); /* src length > dst_size = truncated */
	ASSERT(ret >= sizeof(buf)); /* truncation indicator */
	ASSERT_STR_EQ("hello", buf);
	PASS();
}

TEST strlcpy_empty(void)
{
	char buf[10] = "garbage";
	rss_strlcpy(buf, "", sizeof(buf));
	ASSERT_STR_EQ("", buf);
	PASS();
}

TEST strlcpy_exact_fit(void)
{
	char buf[6];
	rss_strlcpy(buf, "hello", sizeof(buf)); /* 5 chars + NUL = 6 */
	ASSERT_STR_EQ("hello", buf);
	PASS();
}

/* ------------------------------------------------------------------ */
/* rss_trim                                                            */
/* ------------------------------------------------------------------ */

TEST trim_both(void)
{
	char buf[] = "  hello  ";
	char *r = rss_trim(buf);
	ASSERT_STR_EQ("hello", r);
	PASS();
}

TEST trim_leading(void)
{
	char buf[] = "  hello";
	char *r = rss_trim(buf);
	ASSERT_STR_EQ("hello", r);
	PASS();
}

TEST trim_trailing(void)
{
	char buf[] = "hello  ";
	char *r = rss_trim(buf);
	ASSERT_STR_EQ("hello", r);
	PASS();
}

TEST trim_none(void)
{
	char buf[] = "hello";
	char *r = rss_trim(buf);
	ASSERT_STR_EQ("hello", r);
	PASS();
}

TEST trim_all_whitespace(void)
{
	char buf[] = "   ";
	char *r = rss_trim(buf);
	ASSERT_STR_EQ("", r);
	PASS();
}

TEST trim_empty(void)
{
	char buf[] = "";
	char *r = rss_trim(buf);
	ASSERT_STR_EQ("", r);
	PASS();
}

/* ------------------------------------------------------------------ */
/* rss_starts_with                                                     */
/* ------------------------------------------------------------------ */

TEST starts_with_match(void)
{
	ASSERT(rss_starts_with("hello world", "hello"));
	PASS();
}

TEST starts_with_no_match(void)
{
	ASSERT_FALSE(rss_starts_with("hello", "world"));
	PASS();
}

TEST starts_with_empty_prefix(void)
{
	ASSERT(rss_starts_with("hello", ""));
	PASS();
}

TEST starts_with_longer_prefix(void)
{
	ASSERT_FALSE(rss_starts_with("hi", "hello"));
	PASS();
}

/* ------------------------------------------------------------------ */
/* rss_secure_compare                                                  */
/* ------------------------------------------------------------------ */

TEST secure_compare_equal(void)
{
	ASSERT(rss_secure_compare("secret", "secret"));
	PASS();
}

TEST secure_compare_differ(void)
{
	ASSERT_FALSE(rss_secure_compare("secret", "secreX"));
	PASS();
}

TEST secure_compare_lengths(void)
{
	ASSERT_FALSE(rss_secure_compare("short", "longer"));
	ASSERT_FALSE(rss_secure_compare("longer", "short"));
	PASS();
}

TEST secure_compare_null(void)
{
	ASSERT_FALSE(rss_secure_compare(NULL, "x"));
	ASSERT_FALSE(rss_secure_compare("x", NULL));
	ASSERT_FALSE(rss_secure_compare(NULL, NULL));
	PASS();
}

TEST secure_compare_overflow(void)
{
	/* Inputs > 255 bytes must return false (not silently truncate) */
	char long_a[300], long_b[300];
	memset(long_a, 'A', sizeof(long_a));
	memset(long_b, 'A', sizeof(long_b));
	long_a[299] = '\0';
	long_b[299] = '\0';
	ASSERT_FALSE(rss_secure_compare(long_a, long_b));
	PASS();
}

TEST secure_compare_length_multiple_256(void)
{
	/* Lengths differing by 256 must not compare as equal
	 * (old bug: unsigned char XOR of lengths wrapped to 0) */
	char a[10], b[266];
	memset(a, 'X', 9);
	a[9] = '\0';
	memset(b, 'X', 265);
	b[265] = '\0';
	ASSERT_FALSE(rss_secure_compare(a, b));
	PASS();
}

SUITE(util_suite)
{
	RUN_TEST(strlcpy_basic);
	RUN_TEST(strlcpy_truncate);
	RUN_TEST(strlcpy_empty);
	RUN_TEST(strlcpy_exact_fit);
	RUN_TEST(trim_both);
	RUN_TEST(trim_leading);
	RUN_TEST(trim_trailing);
	RUN_TEST(trim_none);
	RUN_TEST(trim_all_whitespace);
	RUN_TEST(trim_empty);
	RUN_TEST(starts_with_match);
	RUN_TEST(starts_with_no_match);
	RUN_TEST(starts_with_empty_prefix);
	RUN_TEST(starts_with_longer_prefix);
	RUN_TEST(secure_compare_equal);
	RUN_TEST(secure_compare_differ);
	RUN_TEST(secure_compare_lengths);
	RUN_TEST(secure_compare_null);
	RUN_TEST(secure_compare_overflow);
	RUN_TEST(secure_compare_length_multiple_256);
}
