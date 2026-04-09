#include "greatest.h"
#include "rss_common.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CFG_PATH "/tmp/rss_test_config.ini"

static void cleanup(void)
{
	unlink(CFG_PATH);
}

/* Helper: write INI content, load, return config (caller frees) */
static rss_config_t *load_ini(const char *content)
{
	int ret = rss_write_file_atomic(CFG_PATH, content, (int)strlen(content));
	if (ret != 0)
		return NULL;
	return rss_config_load(CFG_PATH);
}

TEST config_load_basic(void)
{
	rss_config_t *cfg = load_ini("[network]\nport = 554\nhost = 0.0.0.0\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "network", "port", ""));
	ASSERT_STR_EQ("0.0.0.0", rss_config_get_str(cfg, "network", "host", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_get_str_default(void)
{
	rss_config_t *cfg = load_ini("[s]\nkey = val\n");
	ASSERT(cfg);
	/* Missing key */
	ASSERT_STR_EQ("default", rss_config_get_str(cfg, "s", "nokey", "default"));
	/* Missing section */
	ASSERT_STR_EQ("default", rss_config_get_str(cfg, "nosec", "key", "default"));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_get_int(void)
{
	rss_config_t *cfg = load_ini("[s]\nport = 8554\nbad = abc\nneg = -10\n");
	ASSERT(cfg);
	ASSERT_EQ(8554, rss_config_get_int(cfg, "s", "port", 0));
	ASSERT_EQ(99, rss_config_get_int(cfg, "s", "bad", 99));
	ASSERT_EQ(-10, rss_config_get_int(cfg, "s", "neg", 0));
	/* Missing key */
	ASSERT_EQ(42, rss_config_get_int(cfg, "s", "nokey", 42));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_get_bool(void)
{
	rss_config_t *cfg = load_ini(
		"[b]\n"
		"a = true\nb = false\nc = yes\nd = no\n"
		"e = 1\nf = 0\ng = on\nh = off\n"
		"i = TRUE\nj = False\nk = YeS\n");
	ASSERT(cfg);
	ASSERT(rss_config_get_bool(cfg, "b", "a", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "b", true));
	ASSERT(rss_config_get_bool(cfg, "b", "c", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "d", true));
	ASSERT(rss_config_get_bool(cfg, "b", "e", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "f", true));
	ASSERT(rss_config_get_bool(cfg, "b", "g", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "h", true));
	/* Case insensitive */
	ASSERT(rss_config_get_bool(cfg, "b", "i", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "j", true));
	ASSERT(rss_config_get_bool(cfg, "b", "k", false));
	/* Missing → default */
	ASSERT(rss_config_get_bool(cfg, "b", "nope", true));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_comments(void)
{
	rss_config_t *cfg = load_ini(
		"# full line comment\n"
		"; also a comment\n"
		"[s]\n"
		"key = value # inline comment\n"
		"plain = nohash\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("value", rss_config_get_str(cfg, "s", "key", ""));
	ASSERT_STR_EQ("nohash", rss_config_get_str(cfg, "s", "plain", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_global_section(void)
{
	rss_config_t *cfg = load_ini("global_key = global_val\n[s]\nkey = val\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("global_val", rss_config_get_str(cfg, "", "global_key", ""));
	ASSERT_STR_EQ("val", rss_config_get_str(cfg, "s", "key", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_case_insensitive(void)
{
	rss_config_t *cfg = load_ini("[Network]\nPort = 554\n");
	ASSERT(cfg);
	/* Section case */
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "network", "Port", ""));
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "NETWORK", "Port", ""));
	/* Key case */
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "Network", "port", ""));
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "Network", "PORT", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_set_str(void)
{
	rss_config_t *cfg = load_ini("[s]\nkey = old\n");
	ASSERT(cfg);
	/* Overwrite existing */
	rss_config_set_str(cfg, "s", "key", "new");
	ASSERT_STR_EQ("new", rss_config_get_str(cfg, "s", "key", ""));
	/* Create new key */
	rss_config_set_str(cfg, "s", "added", "fresh");
	ASSERT_STR_EQ("fresh", rss_config_get_str(cfg, "s", "added", ""));
	/* Create new section + key */
	rss_config_set_str(cfg, "new_sec", "k", "v");
	ASSERT_STR_EQ("v", rss_config_get_str(cfg, "new_sec", "k", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_set_int(void)
{
	rss_config_t *cfg = load_ini("[s]\n");
	ASSERT(cfg);
	rss_config_set_int(cfg, "s", "port", 8080);
	ASSERT_EQ(8080, rss_config_get_int(cfg, "s", "port", 0));
	rss_config_set_int(cfg, "s", "neg", -42);
	ASSERT_EQ(-42, rss_config_get_int(cfg, "s", "neg", 0));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_save_roundtrip(void)
{
	rss_config_t *cfg = load_ini("[a]\nk1 = v1\nk2 = v2\n[b]\nk3 = v3\n");
	ASSERT(cfg);
	rss_config_set_str(cfg, "a", "k4", "v4");

	const char *save_path = "/tmp/rss_test_config_save.ini";
	ASSERT_EQ(0, rss_config_save(cfg, save_path));
	rss_config_free(cfg);

	/* Reload and verify */
	rss_config_t *cfg2 = rss_config_load(save_path);
	ASSERT(cfg2);
	ASSERT_STR_EQ("v1", rss_config_get_str(cfg2, "a", "k1", ""));
	ASSERT_STR_EQ("v2", rss_config_get_str(cfg2, "a", "k2", ""));
	ASSERT_STR_EQ("v4", rss_config_get_str(cfg2, "a", "k4", ""));
	ASSERT_STR_EQ("v3", rss_config_get_str(cfg2, "b", "k3", ""));
	rss_config_free(cfg2);
	unlink(save_path);
	cleanup();
	PASS();
}

struct foreach_ctx {
	int count;
	char keys[16][64];
	char vals[16][256];
};

static void foreach_cb(const char *key, const char *value, void *userdata)
{
	struct foreach_ctx *ctx = userdata;
	if (ctx->count < 16) {
		rss_strlcpy(ctx->keys[ctx->count], key, 64);
		rss_strlcpy(ctx->vals[ctx->count], value, 256);
		ctx->count++;
	}
}

TEST config_foreach(void)
{
	rss_config_t *cfg = load_ini("[s]\na = 1\nb = 2\nc = 3\n");
	ASSERT(cfg);
	struct foreach_ctx ctx = {0};
	int n = rss_config_foreach(cfg, "s", foreach_cb, &ctx);
	ASSERT_EQ(3, n);
	ASSERT_EQ(3, ctx.count);
	/* Verify all keys present (order may be reversed due to linked list) */
	int found = 0;
	for (int i = 0; i < ctx.count; i++) {
		if (strcmp(ctx.keys[i], "a") == 0) { ASSERT_STR_EQ("1", ctx.vals[i]); found++; }
		if (strcmp(ctx.keys[i], "b") == 0) { ASSERT_STR_EQ("2", ctx.vals[i]); found++; }
		if (strcmp(ctx.keys[i], "c") == 0) { ASSERT_STR_EQ("3", ctx.vals[i]); found++; }
	}
	ASSERT_EQ(3, found);
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_empty_file(void)
{
	rss_config_t *cfg = load_ini("");
	ASSERT(cfg);
	ASSERT_STR_EQ("default", rss_config_get_str(cfg, "s", "k", "default"));
	ASSERT_EQ(99, rss_config_get_int(cfg, "s", "k", 99));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_long_value(void)
{
	/* MAX_VAL is 256, so 255 chars should fit */
	char ini[1024];
	char long_val[260];
	memset(long_val, 'X', 259);
	long_val[259] = '\0';
	snprintf(ini, sizeof(ini), "[s]\nkey = %s\n", long_val);

	rss_config_t *cfg = load_ini(ini);
	ASSERT(cfg);
	const char *v = rss_config_get_str(cfg, "s", "key", "");
	/* Value should be truncated to MAX_VAL-1 = 255 chars */
	ASSERT(strlen(v) == 255);
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_duplicate_key(void)
{
	rss_config_t *cfg = load_ini("[s]\nkey = first\nkey = second\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("second", rss_config_get_str(cfg, "s", "key", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

SUITE(config_suite)
{
	RUN_TEST(config_load_basic);
	RUN_TEST(config_get_str_default);
	RUN_TEST(config_get_int);
	RUN_TEST(config_get_bool);
	RUN_TEST(config_comments);
	RUN_TEST(config_global_section);
	RUN_TEST(config_case_insensitive);
	RUN_TEST(config_set_str);
	RUN_TEST(config_set_int);
	RUN_TEST(config_save_roundtrip);
	RUN_TEST(config_foreach);
	RUN_TEST(config_empty_file);
	RUN_TEST(config_long_value);
	RUN_TEST(config_duplicate_key);
}
