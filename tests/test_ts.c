#include "greatest.h"
#include "rss_ts.h"

#include <string.h>

/* Locate the ES_info descriptor block that follows the 'Opus'
 * registration in a freshly written PMT. Returns a pointer to the
 * registration_descriptor tag (0x05) or NULL. */
static const uint8_t *find_opus_descriptors(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i + 4 < n; i++) {
        if (buf[i] == 'O' && buf[i + 1] == 'p' && buf[i + 2] == 'u' && buf[i + 3] == 's')
            return buf + i - 2; /* back up to the 0x05 0x04 tag/len */
    }
    return NULL;
}

TEST opus_pmt_carries_channel_descriptor(void)
{
    rss_ts_mux_t m;
    rss_ts_init(&m, 0x24 /* HEVC */, RSS_TS_STREAM_OPUS, 1 /* mono */, 0);
    uint8_t buf[512];
    size_t n = rss_ts_write_pat_pmt(&m, buf, sizeof buf);
    ASSERT(n > 0);

    const uint8_t *d = find_opus_descriptors(buf, n);
    ASSERT(d != NULL);
    /* registration_descriptor: tag 0x05, len 0x04, "Opus" */
    ASSERT_EQ(0x05, d[0]);
    ASSERT_EQ(0x04, d[1]);
    ASSERT_EQ(0, memcmp(d + 2, "Opus", 4));
    /* Opus audio descriptor: extension tag 0x7F, len 0x02, ext 0x80,
     * channel_config = 1 (must be present, or demuxers assume stereo) */
    ASSERT_EQ(0x7F, d[6]);
    ASSERT_EQ(0x02, d[7]);
    ASSERT_EQ(0x80, d[8]);
    ASSERT_EQ(1, d[9]);
    PASS();
}

TEST opus_channel_config_reflects_init(void)
{
    rss_ts_mux_t m;
    rss_ts_init(&m, 0x24, RSS_TS_STREAM_OPUS, 2 /* stereo */, 0);
    uint8_t buf[512];
    size_t n = rss_ts_write_pat_pmt(&m, buf, sizeof buf);
    const uint8_t *d = find_opus_descriptors(buf, n);
    ASSERT(d != NULL);
    ASSERT_EQ(2, d[9]);
    /* zero channels is coerced to mono so the descriptor is never 0 */
    rss_ts_init(&m, 0x24, RSS_TS_STREAM_OPUS, 0, 0);
    n = rss_ts_write_pat_pmt(&m, buf, sizeof buf);
    d = find_opus_descriptors(buf, n);
    ASSERT(d != NULL);
    ASSERT_EQ(1, d[9]);
    PASS();
}

TEST aac_pmt_has_no_opus_descriptor(void)
{
    rss_ts_mux_t m;
    rss_ts_init(&m, 0x24, RSS_TS_STREAM_AAC, 1, 0);
    uint8_t buf[512];
    size_t n = rss_ts_write_pat_pmt(&m, buf, sizeof buf);
    ASSERT(n > 0);
    ASSERT(find_opus_descriptors(buf, n) == NULL);
    PASS();
}

/* PCR must be strictly monotonic even when the DTS-derived value
 * clamps at a rebased stream head (frame interval < the 33ms lead). */
TEST ts_pcr_monotonic_at_head(void)
{
    rss_ts_mux_t m;
    rss_ts_init(&m, RSS_TS_STREAM_H264, RSS_TS_STREAM_NONE, 1, 0);
    uint8_t buf[4096];
    uint8_t frame[64] = {0};
    uint64_t last = 0;
    bool have_last = false;
    for (int i = 0; i < 4; i++) {
        uint64_t dts = (uint64_t)i * 2320; /* < 3000-tick lead */
        size_t n = rss_ts_write_video(&m, buf, sizeof(buf), frame, sizeof(frame), dts, dts, i == 0);
        ASSERT(n > 0);
        for (size_t off = 0; off + 188 <= n; off += 188) {
            uint8_t *p = buf + off;
            int afc = (p[3] >> 4) & 3;
            if ((afc == 2 || afc == 3) && p[4] >= 7 && (p[5] & 0x10)) {
                uint64_t base = ((uint64_t)p[6] << 25) | ((uint64_t)p[7] << 17) |
                                ((uint64_t)p[8] << 9) | ((uint64_t)p[9] << 1) | (p[10] >> 7);
                if (have_last)
                    ASSERT(base > last);
                last = base;
                have_last = true;
            }
        }
    }
    ASSERT(have_last);
    PASS();
}

SUITE(ts_suite)
{
    RUN_TEST(ts_pcr_monotonic_at_head);
    RUN_TEST(opus_pmt_carries_channel_descriptor);
    RUN_TEST(opus_channel_config_reflects_init);
    RUN_TEST(aac_pmt_has_no_opus_descriptor);
}
