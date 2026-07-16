#include "greatest.h"
#include "rss_jpeg.h"
#include "rss_sign.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_UTC 1784269441123456ULL /* 2026-07-16T... .123456 */

/* Minimal fake JPEG: SOI, APP0 stub, fake scan bytes, EOI */
static size_t make_jpeg(uint8_t *buf)
{
    size_t n = 0;
    buf[n++] = 0xff;
    buf[n++] = 0xd8; /* SOI */
    const uint8_t app0[] = {0xff, 0xe0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0,
                            1,    2,    0,    0,    1,   0,   1,   0,   0};
    memcpy(buf + n, app0, sizeof(app0));
    n += sizeof(app0);
    const uint8_t sos[] = {0xff, 0xda, 0x00, 0x08, 1, 1, 0, 0, 0x3f, 0};
    memcpy(buf + n, sos, sizeof(sos));
    n += sizeof(sos);
    for (int i = 0; i < 64; i++)
        buf[n++] = (uint8_t)(i * 7 + 1); /* fake entropy, no 0xff */
    buf[n++] = 0xff;
    buf[n++] = 0xd9; /* EOI */
    return n;
}

static int make_key(rss_sign_key_t *key, char *path_out)
{
    char tmpl[] = "/tmp/rss_jpeg_test_key_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
        return -1;
    close(fd);
    unlink(tmpl);
    strcpy(path_out, tmpl);
    return rss_sign_key_load(key, tmpl);
}

TEST jpeg_exif_roundtrip(void)
{
    uint8_t buf[1024];
    size_t len = make_jpeg(buf);

    int n = rss_jpeg_insert_exif(buf, sizeof(buf), len, TEST_UTC);
    ASSERT(n > (int)len);

    /* still starts SOI, ends EOI, APP1 right after SOI */
    ASSERT_EQ(0xff, buf[0]);
    ASSERT_EQ(0xd8, buf[1]);
    ASSERT_EQ(0xff, buf[2]);
    ASSERT_EQ(0xe1, buf[3]);
    ASSERT_EQ(0xd9, buf[n - 1]);

    uint64_t utc = 0;
    ASSERT_EQ(0, rss_jpeg_get_exif_time(buf, (size_t)n, &utc));
    ASSERT_EQ(TEST_UTC, utc);
    PASS();
}

TEST jpeg_sign_verify_tamper(void)
{
    rss_sign_key_t key;
    char key_path[64];
    ASSERT_EQ(0, make_key(&key, key_path));

    uint8_t buf[1024];
    size_t len = make_jpeg(buf);
    int n = rss_jpeg_insert_exif(buf, sizeof(buf), len, TEST_UTC);
    ASSERT(n > 0);
    n = rss_jpeg_sign(buf, sizeof(buf), (size_t)n, &key);
    ASSERT(n > 0);

    /* structure: ends with EOI, APP15 before it */
    ASSERT_EQ(0xd9, buf[n - 1]);
    ASSERT_EQ(0xef, buf[n - 2 - RSS_JPEG_SIG_SEGMENT + 1]);

    uint8_t fp[8];
    ASSERT_EQ(0, rss_jpeg_verify(buf, (size_t)n, key.public, fp));
    ASSERT_MEM_EQ(key.fingerprint, fp, 8);

    /* presence check without a key */
    uint8_t fp2[8];
    ASSERT_EQ(1, rss_jpeg_verify(buf, (size_t)n, NULL, fp2));
    ASSERT_MEM_EQ(key.fingerprint, fp2, 8);

    /* EXIF still readable after signing */
    uint64_t utc = 0;
    ASSERT_EQ(0, rss_jpeg_get_exif_time(buf, (size_t)n, &utc));
    ASSERT_EQ(TEST_UTC, utc);

    /* tamper with image data — signature fails */
    uint8_t bad[1024];
    memcpy(bad, buf, (size_t)n);
    bad[40] ^= 0x01;
    ASSERT_EQ(-EBADMSG, rss_jpeg_verify(bad, (size_t)n, key.public, NULL));

    /* tamper with the EXIF timestamp — also fails (covered by sig) */
    memcpy(bad, buf, (size_t)n);
    bad[80] ^= 0x01; /* inside APP1 */
    ASSERT_EQ(-EBADMSG, rss_jpeg_verify(bad, (size_t)n, key.public, NULL));

    /* wrong key fails */
    rss_sign_key_t other;
    char other_path[64];
    ASSERT_EQ(0, make_key(&other, other_path));
    ASSERT_EQ(-EBADMSG, rss_jpeg_verify(buf, (size_t)n, other.public, NULL));

    /* unsigned image reports no signature */
    uint8_t plain[1024];
    size_t plen = make_jpeg(plain);
    ASSERT_EQ(-ENOENT, rss_jpeg_verify(plain, plen, key.public, NULL));

    char p2[80];
    snprintf(p2, sizeof(p2), "%s.pub", key_path);
    unlink(key_path);
    unlink(p2);
    snprintf(p2, sizeof(p2), "%s.pub", other_path);
    unlink(other_path);
    unlink(p2);
    PASS();
}

TEST jpeg_bounds(void)
{
    uint8_t buf[128];
    size_t len = make_jpeg(buf);
    /* cap too small for the EXIF segment */
    ASSERT_EQ(-ENOSPC, rss_jpeg_insert_exif(buf, len + 10, len, TEST_UTC));

    uint8_t notjpeg[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    ASSERT_EQ(-EINVAL, rss_jpeg_insert_exif(notjpeg, sizeof(notjpeg), 8, TEST_UTC));
    PASS();
}

SUITE(jpeg_suite)
{
    RUN_TEST(jpeg_exif_roundtrip);
    RUN_TEST(jpeg_sign_verify_tamper);
    RUN_TEST(jpeg_bounds);
}
