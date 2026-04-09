#include "greatest.h"
#include "rss_common.h"

#include <string.h>

TEST json_get_str_basic(void)
{
	char buf[64];
	int ret = rss_json_get_str("{\"cmd\":\"status\"}", "cmd", buf, sizeof(buf));
	ASSERT_EQ(0, ret);
	ASSERT_STR_EQ("status", buf);
	PASS();
}

TEST json_get_str_missing(void)
{
	char buf[64];
	int ret = rss_json_get_str("{\"cmd\":\"status\"}", "nokey", buf, sizeof(buf));
	ASSERT_EQ(-1, ret);
	PASS();
}

TEST json_get_str_truncate(void)
{
	char buf[4];
	int ret = rss_json_get_str("{\"k\":\"longvalue\"}", "k", buf, sizeof(buf));
	ASSERT_EQ(0, ret);
	ASSERT_STR_EQ("lon", buf);
	PASS();
}

TEST json_get_int_basic(void)
{
	int val = 0;
	int ret = rss_json_get_int("{\"count\":42}", "count", &val);
	ASSERT_EQ(0, ret);
	ASSERT_EQ(42, val);
	PASS();
}

TEST json_get_int_negative(void)
{
	int val = 0;
	int ret = rss_json_get_int("{\"val\":-5}", "val", &val);
	ASSERT_EQ(0, ret);
	ASSERT_EQ(-5, val);
	PASS();
}

TEST json_get_int_missing(void)
{
	int val = 99;
	int ret = rss_json_get_int("{\"a\":1}", "b", &val);
	ASSERT_EQ(-1, ret);
	ASSERT_EQ(99, val); /* unchanged */
	PASS();
}

SUITE(json_suite)
{
	RUN_TEST(json_get_str_basic);
	RUN_TEST(json_get_str_missing);
	RUN_TEST(json_get_str_truncate);
	RUN_TEST(json_get_int_basic);
	RUN_TEST(json_get_int_negative);
	RUN_TEST(json_get_int_missing);
}
