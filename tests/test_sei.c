#include "greatest.h"
#include "rss_sei.h"

#include <errno.h>
#include <string.h>

/* 2026-07-15T06:24:01.123456Z-ish */
#define TEST_UTC 1784269441123456ULL

TEST sei_build_h264_layout(void)
{
    uint8_t buf[RSS_SEI_TS_MAX];
    int n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_NONE,
                                    TEST_UTC, RSS_SEI_TS_LOCKED);
    ASSERT_EQ(32, n);
    ASSERT_EQ(0x06, buf[0]); /* SEI NAL, nal_ref_idc 0 */
    ASSERT_EQ(0x05, buf[1]); /* user_data_unregistered */
    ASSERT_EQ(0x1c, buf[2]); /* payload size 28 */
    ASSERT_MEM_EQ("MISPmicrosectime", buf + 3, 16);
    ASSERT_EQ(0x1f, buf[19]); /* status */
    /* guard bytes */
    ASSERT_EQ(0xff, buf[22]);
    ASSERT_EQ(0xff, buf[25]);
    ASSERT_EQ(0xff, buf[28]);
    /* big-endian value around the guards */
    uint64_t v = ((uint64_t)buf[20] << 56) | ((uint64_t)buf[21] << 48) | ((uint64_t)buf[23] << 40) |
                 ((uint64_t)buf[24] << 32) | ((uint64_t)buf[26] << 24) | ((uint64_t)buf[27] << 16) |
                 ((uint64_t)buf[29] << 8) | (uint64_t)buf[30];
    ASSERT_EQ(TEST_UTC, v);
    ASSERT_EQ(0x80, buf[31]); /* rbsp trailing */
    PASS();
}

TEST sei_build_h265_layout(void)
{
    uint8_t buf[RSS_SEI_TS_MAX];
    int n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H265, RSS_SEI_PREFIX_NONE,
                                    TEST_UTC, RSS_SEI_TS_LOCKED);
    ASSERT_EQ(33, n);
    ASSERT_EQ(0x4e, buf[0]); /* PREFIX_SEI_NUT */
    ASSERT_EQ(0x01, buf[1]);
    ASSERT_EQ(0x05, buf[2]);
    ASSERT_EQ(0x1c, buf[3]);
    const uint8_t h265_uuid[16] = {0xa8, 0x68, 0x7d, 0xd4, 0xd7, 0x59, 0x37, 0x58,
                                   0xa5, 0xce, 0xf0, 0x33, 0x8b, 0x65, 0x45, 0xf1};
    ASSERT_MEM_EQ(h265_uuid, buf + 4, 16);
    PASS();
}

TEST sei_build_prefixes(void)
{
    uint8_t buf[RSS_SEI_TS_MAX];

    int n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_ANNEXB,
                                    TEST_UTC, RSS_SEI_TS_LOCKED);
    ASSERT_EQ(36, n);
    ASSERT_EQ(0x00, buf[0]);
    ASSERT_EQ(0x00, buf[1]);
    ASSERT_EQ(0x00, buf[2]);
    ASSERT_EQ(0x01, buf[3]);
    ASSERT_EQ(0x06, buf[4]);

    n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_AVCC, TEST_UTC,
                                RSS_SEI_TS_LOCKED);
    ASSERT_EQ(36, n);
    uint32_t nal_len = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                       ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    ASSERT_EQ(32u, nal_len);
    ASSERT_EQ(0x06, buf[4]);
    PASS();
}

TEST sei_roundtrip(void)
{
    uint8_t buf[RSS_SEI_TS_MAX];
    uint64_t utc = 0;
    uint8_t status = 0;

    int n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_NONE,
                                    TEST_UTC, RSS_SEI_TS_UNLOCKED);
    ASSERT(n > 0);
    ASSERT_EQ(0, rss_sei_parse_timestamp(buf, (size_t)n, RSS_SEI_CODEC_H264, &utc, &status));
    ASSERT_EQ(TEST_UTC, utc);
    ASSERT_EQ(RSS_SEI_TS_UNLOCKED, status);

    n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H265, RSS_SEI_PREFIX_NONE, TEST_UTC,
                                RSS_SEI_TS_LOCKED);
    ASSERT(n > 0);
    ASSERT_EQ(0, rss_sei_parse_timestamp(buf, (size_t)n, RSS_SEI_CODEC_H265, &utc, &status));
    ASSERT_EQ(TEST_UTC, utc);
    ASSERT_EQ(RSS_SEI_TS_LOCKED, status);
    PASS();
}

/* Assert no 00 00 0x pattern anywhere in the bare NAL — the layout
 * makes start-code emulation impossible, verify across edge values. */
static int has_emulation(const uint8_t *p, int len)
{
    for (int i = 0; i + 2 < len; i++) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] <= 3)
            return 1;
    }
    return 0;
}

TEST sei_no_start_code_emulation(void)
{
    const uint64_t values[] = {
        0,
        1,
        0x0000000000000100ULL,
        0x0000030000000300ULL,
        TEST_UTC,
        0x00065f3000000000ULL, /* µs with zero low bytes */
        0xffffffffffffffffULL,
    };

    uint8_t buf[RSS_SEI_TS_MAX];
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        for (int codec = 0; codec <= 1; codec++) {
            /* status 0x00 exercises the reserved-bits forcing */
            int n = rss_sei_build_timestamp(buf, sizeof(buf), codec, RSS_SEI_PREFIX_NONE, values[i],
                                            0x00);
            ASSERT(n > 0);
            ASSERT_FALSE(has_emulation(buf, n));
        }
    }
    PASS();
}

TEST sei_parse_rejects(void)
{
    uint8_t buf[RSS_SEI_TS_MAX];
    uint64_t utc;

    int n = rss_sei_build_timestamp(buf, sizeof(buf), RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_NONE,
                                    TEST_UTC, RSS_SEI_TS_LOCKED);
    ASSERT(n > 0);

    /* wrong codec: H.264 NAL parsed as H.265 */
    ASSERT_EQ(-ENOENT, rss_sei_parse_timestamp(buf, (size_t)n, RSS_SEI_CODEC_H265, &utc, NULL));

    /* non-SEI NAL type */
    uint8_t idr[8] = {0x65, 0x88, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    ASSERT_EQ(-ENOENT, rss_sei_parse_timestamp(idr, sizeof(idr), RSS_SEI_CODEC_H264, &utc, NULL));

    /* SEI with a different UUID */
    uint8_t other[32];
    memcpy(other, buf, (size_t)n);
    other[3] ^= 0xa5;
    ASSERT_EQ(-ENOENT, rss_sei_parse_timestamp(other, (size_t)n, RSS_SEI_CODEC_H264, &utc, NULL));

    /* corrupted guard byte */
    memcpy(other, buf, (size_t)n);
    other[22] = 0x00;
    ASSERT_EQ(-EBADMSG, rss_sei_parse_timestamp(other, (size_t)n, RSS_SEI_CODEC_H264, &utc, NULL));

    /* truncated */
    ASSERT_EQ(-ENOENT, rss_sei_parse_timestamp(buf, 10, RSS_SEI_CODEC_H264, &utc, NULL));

    /* buffer too small to build */
    ASSERT_EQ(-ENOSPC, rss_sei_build_timestamp(buf, 8, RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_NONE,
                                               TEST_UTC, RSS_SEI_TS_LOCKED));
    PASS();
}

TEST sei_parse_skips_other_messages(void)
{
    /* SEI NAL carrying a foreign message (type 1, 4 bytes) before ours */
    uint8_t nal[64];
    int off = 0;
    nal[off++] = 0x06;
    nal[off++] = 0x01; /* pic_timing */
    nal[off++] = 0x04;
    nal[off++] = 0xde;
    nal[off++] = 0xad;
    nal[off++] = 0xbe;
    nal[off++] = 0xef;

    uint8_t ours[RSS_SEI_TS_MAX];
    int n = rss_sei_build_timestamp(ours, sizeof(ours), RSS_SEI_CODEC_H264, RSS_SEI_PREFIX_NONE,
                                    TEST_UTC, RSS_SEI_TS_LOCKED);
    ASSERT(n > 0);
    /* append our message body (skip its NAL header byte, keep trailing) */
    memcpy(nal + off, ours + 1, (size_t)(n - 1));
    off += n - 1;

    uint64_t utc = 0;
    ASSERT_EQ(0, rss_sei_parse_timestamp(nal, (size_t)off, RSS_SEI_CODEC_H264, &utc, NULL));
    ASSERT_EQ(TEST_UTC, utc);
    PASS();
}

SUITE(sei_suite)
{
    RUN_TEST(sei_build_h264_layout);
    RUN_TEST(sei_build_h265_layout);
    RUN_TEST(sei_build_prefixes);
    RUN_TEST(sei_roundtrip);
    RUN_TEST(sei_no_start_code_emulation);
    RUN_TEST(sei_parse_rejects);
    RUN_TEST(sei_parse_skips_other_messages);
}
