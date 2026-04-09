#include "greatest.h"
#include "rss_common.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DIR "/tmp/rss_test_file"

static void file_cleanup(void)
{
	unlink(TEST_DIR "/data.txt");
	unlink(TEST_DIR "/data.txt.tmp");
	unlink(TEST_DIR "/empty.txt");
	rmdir(TEST_DIR);
}

/* ------------------------------------------------------------------ */
/* rss_read_file / rss_write_file_atomic                               */
/* ------------------------------------------------------------------ */

TEST read_file_basic(void)
{
	const char *path = TEST_DIR "/data.txt";
	rss_mkdir_p(TEST_DIR);
	const char *content = "hello world\n";
	ASSERT_EQ(0, rss_write_file_atomic(path, content, (int)strlen(content)));

	int size = 0;
	char *buf = rss_read_file(path, &size);
	ASSERT(buf);
	ASSERT_EQ((int)strlen(content), size);
	ASSERT_STR_EQ(content, buf);
	free(buf);
	file_cleanup();
	PASS();
}

TEST read_file_missing(void)
{
	char *buf = rss_read_file("/tmp/rss_test_NONEXISTENT_FILE", NULL);
	ASSERT_EQ(NULL, buf);
	PASS();
}

TEST read_file_empty(void)
{
	const char *path = TEST_DIR "/empty.txt";
	rss_mkdir_p(TEST_DIR);
	ASSERT_EQ(0, rss_write_file_atomic(path, "", 0));

	int size = -1;
	char *buf = rss_read_file(path, &size);
	ASSERT(buf);
	ASSERT_EQ(0, size);
	free(buf);
	file_cleanup();
	PASS();
}

TEST write_file_atomic(void)
{
	const char *path = TEST_DIR "/data.txt";
	rss_mkdir_p(TEST_DIR);
	const char *data = "atomic content";
	ASSERT_EQ(0, rss_write_file_atomic(path, data, (int)strlen(data)));

	/* Verify content */
	int size = 0;
	char *buf = rss_read_file(path, &size);
	ASSERT(buf);
	ASSERT_EQ((int)strlen(data), size);
	ASSERT_MEM_EQ(data, buf, (unsigned)size);
	free(buf);

	/* Verify no .tmp leftover */
	struct stat st;
	char tmp_path[256];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	ASSERT(stat(tmp_path, &st) < 0); /* should not exist */

	file_cleanup();
	PASS();
}

TEST write_file_atomic_overwrite(void)
{
	const char *path = TEST_DIR "/data.txt";
	rss_mkdir_p(TEST_DIR);
	ASSERT_EQ(0, rss_write_file_atomic(path, "old", 3));
	ASSERT_EQ(0, rss_write_file_atomic(path, "new content", 11));

	int size = 0;
	char *buf = rss_read_file(path, &size);
	ASSERT(buf);
	ASSERT_EQ(11, size);
	ASSERT_STR_EQ("new content", buf);
	free(buf);
	file_cleanup();
	PASS();
}

/* ------------------------------------------------------------------ */
/* rss_mkdir_p                                                         */
/* ------------------------------------------------------------------ */

TEST mkdir_p_nested(void)
{
	const char *path = "/tmp/rss_test_mkdir/a/b/c";
	ASSERT_EQ(0, rss_mkdir_p(path));
	struct stat st;
	ASSERT_EQ(0, stat(path, &st));
	ASSERT(S_ISDIR(st.st_mode));
	/* Cleanup */
	rmdir("/tmp/rss_test_mkdir/a/b/c");
	rmdir("/tmp/rss_test_mkdir/a/b");
	rmdir("/tmp/rss_test_mkdir/a");
	rmdir("/tmp/rss_test_mkdir");
	PASS();
}

TEST mkdir_p_existing(void)
{
	ASSERT_EQ(0, rss_mkdir_p("/tmp")); /* always exists */
	PASS();
}

/* ------------------------------------------------------------------ */
/* rss_format_timestamp                                                */
/* ------------------------------------------------------------------ */

TEST timestamp_format(void)
{
	char buf[32];
	char *ret = rss_format_timestamp(buf, sizeof(buf));
	ASSERT_EQ(buf, ret);
	/* Format: "YYYY-MM-DD HH:MM:SS" → 19 chars */
	ASSERT_EQ(19, (int)strlen(buf));
	/* Basic structure checks */
	ASSERT_EQ('-', buf[4]);
	ASSERT_EQ('-', buf[7]);
	ASSERT_EQ(' ', buf[10]);
	ASSERT_EQ(':', buf[13]);
	ASSERT_EQ(':', buf[16]);
	PASS();
}

TEST timestamp_custom_fmt(void)
{
	char buf[32];
	rss_format_timestamp_fmt(buf, sizeof(buf), "%H:%M");
	/* Should produce something like "14:30" — at least 4 chars with colon */
	ASSERT(strlen(buf) >= 4);
	ASSERT_EQ(':', buf[2]);
	PASS();
}

SUITE(file_suite)
{
	RUN_TEST(read_file_basic);
	RUN_TEST(read_file_missing);
	RUN_TEST(read_file_empty);
	RUN_TEST(write_file_atomic);
	RUN_TEST(write_file_atomic_overwrite);
	RUN_TEST(mkdir_p_nested);
	RUN_TEST(mkdir_p_existing);
	RUN_TEST(timestamp_format);
	RUN_TEST(timestamp_custom_fmt);
}
