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

/* Simulate two daemons saving to the same file — only dirty keys merge */
TEST config_save_dirty_merge(void)
{
	const char *path = "/tmp/rss_test_config_merge.ini";

	/* Write initial shared config */
	rss_config_t *init = load_ini("[audio]\ncodec = aac\nvolume = 80\n[stream0]\nfps = 25\n");
	ASSERT(init);
	rss_config_set_str(init, "audio", "codec", "aac"); /* mark all dirty for initial write */
	rss_config_set_str(init, "audio", "volume", "80");
	rss_config_set_str(init, "stream0", "fps", "25");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	/* Daemon A (RVD) loads config, changes stream0/fps */
	rss_config_t *daemon_a = rss_config_load(path);
	ASSERT(daemon_a);
	rss_config_set_int(daemon_a, "stream0", "fps", 15);
	/* daemon_a also has [audio] from file load — but NOT dirty */

	/* Daemon B (RAD) loads config, changes audio/volume */
	rss_config_t *daemon_b = rss_config_load(path);
	ASSERT(daemon_b);
	rss_config_set_int(daemon_b, "audio", "volume", 50);
	/* daemon_b also has [stream0] from file load — but NOT dirty */

	/* Both save sequentially (like raptorctl config save) */
	ASSERT_EQ(0, rss_config_save(daemon_a, path));
	ASSERT_EQ(0, rss_config_save(daemon_b, path));
	rss_config_free(daemon_a);
	rss_config_free(daemon_b);

	/* Verify: both changes survived, neither clobbered the other */
	rss_config_t *result = rss_config_load(path);
	ASSERT(result);
	ASSERT_STR_EQ("aac", rss_config_get_str(result, "audio", "codec", ""));
	ASSERT_EQ(50, rss_config_get_int(result, "audio", "volume", 0));
	ASSERT_EQ(15, rss_config_get_int(result, "stream0", "fps", 0));
	rss_config_free(result);
	unlink(path);
	cleanup();
	PASS();
}

/* Defaults populated by get_* must NOT be dirty */
TEST config_save_defaults_not_dirty(void)
{
	const char *path = "/tmp/rss_test_config_defaults.ini";

	/* Write minimal config */
	rss_config_t *init = load_ini("[audio]\ncodec = opus\n");
	ASSERT(init);
	rss_config_set_str(init, "audio", "codec", "opus");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	/* Daemon loads config, reads a key with default (populates it) */
	rss_config_t *daemon = rss_config_load(path);
	ASSERT(daemon);
	int vol = rss_config_get_int(daemon, "audio", "volume", 80);
	ASSERT_EQ(80, vol);
	/* volume=80 is now in memory but NOT dirty */

	/* Another process writes volume=50 to the file */
	rss_config_t *other = rss_config_load(path);
	ASSERT(other);
	rss_config_set_int(other, "audio", "volume", 50);
	ASSERT_EQ(0, rss_config_save(other, path));
	rss_config_free(other);

	/* Original daemon saves — should NOT overwrite volume=50 with default 80 */
	ASSERT_EQ(0, rss_config_save(daemon, path));
	rss_config_free(daemon);

	rss_config_t *result = rss_config_load(path);
	ASSERT(result);
	ASSERT_EQ(50, rss_config_get_int(result, "audio", "volume", 0));
	ASSERT_STR_EQ("opus", rss_config_get_str(result, "audio", "codec", ""));
	rss_config_free(result);
	unlink(path);
	cleanup();
	PASS();
}

/*
 * Multi-daemon config save stress test.
 *
 * Simulates the real raptor daemon lifecycle: 7 daemons share one config
 * file, each owns different sections, runtime changes are made via
 * rss_config_set_*, and raptorctl broadcasts config-save to all daemons
 * sequentially.  We run 100 cycles and verify no data loss or corruption.
 */

#define STRESS_PATH "/tmp/rss_test_config_stress.ini"
#define NUM_DAEMONS 7
#define NUM_CYCLES 100

/* Daemon section ownership — mirrors real raptor */
/* Daemon names for documentation — matches daemon_sections[] order */
/* static const char *daemon_names[] = {"rvd","rsd","rad","rod","rhd","ric","rwd"}; */

/* Each daemon's owned sections */
static const char *daemon_sections[][4] = {
	{"stream0", "stream1", "image", NULL},   /* rvd */
	{"rtsp", NULL, NULL, NULL},              /* rsd */
	{"audio", NULL, NULL, NULL},             /* rad */
	{"osd", NULL, NULL, NULL},               /* rod */
	{"http", NULL, NULL, NULL},              /* rhd */
	{"ircut", NULL, NULL, NULL},             /* ric */
	{"webrtc", "webtorrent", NULL, NULL},    /* rwd */
};

/* Simulated keys per section */
static const char *section_keys[][4] = {
	{"fps", "bitrate", "gop", NULL},            /* stream0 */
	{"fps", "bitrate", NULL, NULL},             /* stream1 */
	{"brightness", "contrast", NULL, NULL},     /* image */
	{"port", "max_clients", NULL, NULL},        /* rtsp */
	{"codec", "volume", "gain", NULL},          /* audio */
	{"text_string", "font_color", NULL, NULL},  /* osd */
	{"port", "max_clients", NULL, NULL},        /* http */
	{"mode", NULL, NULL, NULL},                 /* ircut */
	{"udp_port", "http_port", NULL, NULL},      /* webrtc */
	{"enabled", NULL, NULL, NULL},              /* webtorrent */
};

/* Map section name → key list index */
static const char *all_sections[] = {
	"stream0", "stream1", "image", "rtsp", "audio",
	"osd", "http", "ircut", "webrtc", "webtorrent"
};

static int find_section_idx(const char *name)
{
	for (int i = 0; i < 10; i++) {
		if (strcmp(all_sections[i], name) == 0)
			return i;
	}
	return -1;
}

TEST config_save_stress_multi_daemon(void)
{
	/* Write initial config with one key per section */
	const char *initial =
		"[stream0]\nfps = 25\nbitrate = 3000000\ngop = 50\n"
		"[stream1]\nfps = 25\nbitrate = 1000000\n"
		"[image]\nbrightness = 128\ncontrast = 128\n"
		"[rtsp]\nport = 554\nmax_clients = 4\n"
		"[audio]\ncodec = aac\nvolume = 80\ngain = 25\n"
		"[osd]\ntext_string = Camera\nfont_color = 0xFFFFFFFF\n"
		"[http]\nport = 8080\nmax_clients = 4\n"
		"[ircut]\nmode = auto\n"
		"[webrtc]\nudp_port = 8443\nhttp_port = 8554\n"
		"[webtorrent]\nenabled = false\n"
		"[log]\nlevel = info\n";

	rss_config_t *seed = load_ini(initial);
	ASSERT(seed);
	/* Mark all dirty for initial write */
	for (int i = 0; i < 10; i++) {
		for (int k = 0; section_keys[i][k]; k++)
			rss_config_set_str(seed, all_sections[i], section_keys[i][k],
					   rss_config_get_str(seed, all_sections[i],
							      section_keys[i][k], ""));
	}
	rss_config_set_str(seed, "log", "level", "info");
	ASSERT_EQ(0, rss_config_save(seed, STRESS_PATH));
	rss_config_free(seed);

	/*
	 * Track expected values. Each cycle, one or more daemons change a
	 * value, then all daemons save sequentially. After each cycle we
	 * reload and verify every key matches expectations.
	 */
	char expected[10][4][64]; /* [section_idx][key_idx][value] */

	/* Initialize expected from initial config */
	const char *init_vals[10][4] = {
		{"25", "3000000", "50", NULL},       /* stream0 */
		{"25", "1000000", NULL, NULL},       /* stream1 */
		{"128", "128", NULL, NULL},          /* image */
		{"554", "4", NULL, NULL},            /* rtsp */
		{"aac", "80", "25", NULL},           /* audio */
		{"Camera", "0xFFFFFFFF", NULL, NULL},/* osd */
		{"8080", "4", NULL, NULL},           /* http */
		{"auto", NULL, NULL, NULL},          /* ircut */
		{"8443", "8554", NULL, NULL},        /* webrtc */
		{"false", NULL, NULL, NULL},         /* webtorrent */
	};

	for (int s = 0; s < 10; s++)
		for (int k = 0; k < 4 && init_vals[s][k]; k++)
			snprintf(expected[s][k], 64, "%s", init_vals[s][k]);

	for (int cycle = 0; cycle < NUM_CYCLES; cycle++) {
		/* Load config for each daemon (simulates daemon startup or
		 * long-running daemon that loaded at boot) */
		rss_config_t *daemons[NUM_DAEMONS];
		for (int d = 0; d < NUM_DAEMONS; d++) {
			daemons[d] = rss_config_load(STRESS_PATH);
			ASSERT(daemons[d]);

			/* Simulate each daemon reading defaults for its sections
			 * (populates in-memory config, must NOT be dirty) */
			for (int si = 0; daemon_sections[d][si]; si++) {
				int idx = find_section_idx(daemon_sections[d][si]);
				if (idx < 0) continue;
				for (int k = 0; section_keys[idx][k]; k++)
					(void)rss_config_get_str(daemons[d],
						all_sections[idx], section_keys[idx][k], "default");
			}

			/* Also read sections owned by OTHER daemons (simulates
			 * shared config load — all sections are in memory) */
			(void)rss_config_get_str(daemons[d], "log", "level", "info");
		}

		/* Pick which daemons modify values this cycle.
		 * Rotate: each cycle, 1-3 daemons make changes. */
		int modifiers[] = {
			cycle % NUM_DAEMONS,
			(cycle * 3 + 1) % NUM_DAEMONS,
			(cycle * 7 + 2) % NUM_DAEMONS
		};
		int nmod = 1 + (cycle % 3); /* 1, 2, or 3 daemons modify */

		for (int m = 0; m < nmod; m++) {
			int d = modifiers[m];
			/* Modify the first key of the daemon's first section */
			const char *sec = daemon_sections[d][0];
			if (!sec) continue;
			int idx = find_section_idx(sec);
			if (idx < 0 || !section_keys[idx][0]) continue;

			char val[64];
			snprintf(val, sizeof(val), "%d", cycle * 100 + d);
			rss_config_set_str(daemons[d], sec, section_keys[idx][0], val);
			snprintf(expected[idx][0], 64, "%s", val);
		}

		/* All daemons save sequentially (like raptorctl config save) */
		for (int d = 0; d < NUM_DAEMONS; d++) {
			ASSERT_EQ(0, rss_config_save(daemons[d], STRESS_PATH));
			rss_config_free(daemons[d]);
		}

		/* Verify: reload file and check every expected key */
		rss_config_t *verify = rss_config_load(STRESS_PATH);
		ASSERT(verify);

		for (int s = 0; s < 10; s++) {
			for (int k = 0; section_keys[s][k]; k++) {
				const char *got = rss_config_get_str(verify,
					all_sections[s], section_keys[s][k], NULL);
				ASSERTm("key missing after save cycle",
					got != NULL);
				if (strcmp(got, expected[s][k]) != 0) {
					fprintf(stderr,
						"MISMATCH cycle %d: [%s] %s = '%s' (expected '%s')\n",
						cycle, all_sections[s], section_keys[s][k],
						got, expected[s][k]);
					FAILm("value mismatch after save cycle");
				}
			}
		}

		/* Also verify [log] section wasn't clobbered */
		ASSERT_STR_EQ("info", rss_config_get_str(verify, "log", "level", ""));
		rss_config_free(verify);
	}

	unlink(STRESS_PATH);
	cleanup();
	PASS();
}

/* Rapid set-then-save on the same key — value must always reflect latest */
TEST config_save_rapid_overwrite(void)
{
	const char *path = "/tmp/rss_test_config_rapid.ini";

	rss_config_t *init = load_ini("[stream0]\nfps = 25\n");
	ASSERT(init);
	rss_config_set_str(init, "stream0", "fps", "25");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	for (int i = 0; i < 100; i++) {
		rss_config_t *cfg = rss_config_load(path);
		ASSERT(cfg);

		char val[16];
		snprintf(val, sizeof(val), "%d", i + 1);
		rss_config_set_str(cfg, "stream0", "fps", val);
		ASSERT_EQ(0, rss_config_save(cfg, path));
		rss_config_free(cfg);

		/* Immediately reload and verify */
		rss_config_t *check = rss_config_load(path);
		ASSERT(check);
		ASSERT_EQ(i + 1, rss_config_get_int(check, "stream0", "fps", -1));
		rss_config_free(check);
	}

	unlink(path);
	cleanup();
	PASS();
}

/* New sections created by set_str must survive cross-daemon saves */
TEST config_save_new_section_survives(void)
{
	const char *path = "/tmp/rss_test_config_newsec.ini";

	/* Start with minimal config — no [stream0] */
	rss_config_t *init = load_ini("[audio]\ncodec = aac\n");
	ASSERT(init);
	rss_config_set_str(init, "audio", "codec", "aac");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	for (int i = 0; i < 50; i++) {
		/* Daemon A creates a new section with a new key */
		rss_config_t *da = rss_config_load(path);
		ASSERT(da);
		char sec[32], val[16];
		snprintf(sec, sizeof(sec), "dynamic%d", i);
		snprintf(val, sizeof(val), "%d", i * 10);
		rss_config_set_str(da, sec, "value", val);
		ASSERT_EQ(0, rss_config_save(da, path));
		rss_config_free(da);

		/* Daemon B saves with no changes — must not clobber new section */
		rss_config_t *db = rss_config_load(path);
		ASSERT(db);
		(void)rss_config_get_str(db, "audio", "codec", "aac");
		ASSERT_EQ(0, rss_config_save(db, path));
		rss_config_free(db);

		/* Verify new section survived */
		rss_config_t *check = rss_config_load(path);
		ASSERT(check);
		ASSERT_EQ(i * 10, rss_config_get_int(check, sec, "value", -1));
		ASSERT_STR_EQ("aac", rss_config_get_str(check, "audio", "codec", ""));
		rss_config_free(check);
	}

	/* Verify ALL dynamic sections survived */
	rss_config_t *final = rss_config_load(path);
	ASSERT(final);
	for (int i = 0; i < 50; i++) {
		char sec[32];
		snprintf(sec, sizeof(sec), "dynamic%d", i);
		ASSERT_EQ(i * 10, rss_config_get_int(final, sec, "value", -1));
	}
	ASSERT_STR_EQ("aac", rss_config_get_str(final, "audio", "codec", ""));
	rss_config_free(final);

	unlink(path);
	cleanup();
	PASS();
}

/* Daemon with no dirty keys must not corrupt the file */
TEST config_save_no_dirty_noop(void)
{
	const char *path = "/tmp/rss_test_config_noop.ini";

	rss_config_t *init = load_ini(
		"[stream0]\nfps = 25\nbitrate = 3000000\n"
		"[audio]\ncodec = aac\nvolume = 80\n");
	ASSERT(init);
	rss_config_set_str(init, "stream0", "fps", "25");
	rss_config_set_str(init, "stream0", "bitrate", "3000000");
	rss_config_set_str(init, "audio", "codec", "aac");
	rss_config_set_str(init, "audio", "volume", "80");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	/* 100 cycles of load→read defaults→save with NO set_str */
	for (int i = 0; i < 100; i++) {
		rss_config_t *cfg = rss_config_load(path);
		ASSERT(cfg);
		/* Read lots of defaults — none should be dirty */
		(void)rss_config_get_int(cfg, "stream0", "gop", 50);
		(void)rss_config_get_bool(cfg, "stream0", "enabled", true);
		(void)rss_config_get_str(cfg, "audio", "sample_rate", "16000");
		(void)rss_config_get_int(cfg, "newdaemon", "newkey", 999);
		ASSERT_EQ(0, rss_config_save(cfg, path));
		rss_config_free(cfg);
	}

	/* File should still have only the original 4 keys */
	rss_config_t *final = rss_config_load(path);
	ASSERT(final);
	ASSERT_EQ(25, rss_config_get_int(final, "stream0", "fps", -1));
	ASSERT_EQ(3000000, rss_config_get_int(final, "stream0", "bitrate", -1));
	ASSERT_STR_EQ("aac", rss_config_get_str(final, "audio", "codec", ""));
	ASSERT_EQ(80, rss_config_get_int(final, "audio", "volume", -1));

	/* Defaults must NOT have leaked to disk */
	/* Reload fresh (no default population) and check raw */
	rss_config_free(final);
	final = rss_config_load(path);
	ASSERT(final);
	const char *gop = rss_config_get_str(final, "stream0", "gop", NULL);
	ASSERTm("default 'gop' leaked to disk", gop == NULL);
	const char *sr = rss_config_get_str(final, "audio", "sample_rate", NULL);
	ASSERTm("default 'sample_rate' leaked to disk", sr == NULL);
	const char *nk = rss_config_get_str(final, "newdaemon", "newkey", NULL);
	ASSERTm("default 'newkey' leaked to disk", nk == NULL);
	rss_config_free(final);

	unlink(path);
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
	RUN_TEST(config_save_dirty_merge);
	RUN_TEST(config_save_defaults_not_dirty);
	RUN_TEST(config_save_stress_multi_daemon);
	RUN_TEST(config_save_rapid_overwrite);
	RUN_TEST(config_save_new_section_survives);
	RUN_TEST(config_save_no_dirty_noop);
	RUN_TEST(config_foreach);
	RUN_TEST(config_empty_file);
	RUN_TEST(config_long_value);
	RUN_TEST(config_duplicate_key);
}
