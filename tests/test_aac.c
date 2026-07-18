/* AAC AudioSpecificConfig builder tests */
#include "greatest.h"
#include <rss_aac.h>

TEST asc_lc_16k_mono(void)
{
    uint8_t buf[RSS_AAC_ASC_MAX];
    int len = rss_aac_asc(RSS_AAC_AOT_LC, 16000, 1, buf);
    ASSERT_EQ(2, len);
    /* AOT=2, idx=8 (16000), ch=1: (2<<11)|(8<<7)|(1<<3) = 0x1408 */
    ASSERT_EQ(0x14, buf[0]);
    ASSERT_EQ(0x08, buf[1]);
    PASS();
}

TEST asc_lc_48k_mono(void)
{
    uint8_t buf[RSS_AAC_ASC_MAX];
    int len = rss_aac_asc(RSS_AAC_AOT_LC, 48000, 1, buf);
    ASSERT_EQ(2, len);
    /* idx=3: (2<<11)|(3<<7)|(1<<3) = 0x1188 */
    ASSERT_EQ(0x11, buf[0]);
    ASSERT_EQ(0x88, buf[1]);
    PASS();
}

TEST asc_he_48k_mono(void)
{
    /* Backward-compatible form, byte-identical to faac's own ASC for
     * 48kHz mono HE-AAC v1 (verified against faac_encoder_asc on HW):
     * LC@24k core + syncExtension(SBR, ext 48k) = 13 08 56 E5 98 */
    uint8_t buf[RSS_AAC_ASC_MAX];
    int len = rss_aac_asc(RSS_AAC_AOT_SBR, 48000, 1, buf);
    ASSERT_EQ(5, len);
    ASSERT_EQ(0x13, buf[0]);
    ASSERT_EQ(0x08, buf[1]);
    ASSERT_EQ(0x56, buf[2]);
    ASSERT_EQ(0xE5, buf[3]);
    ASSERT_EQ(0x98, buf[4]);
    PASS();
}

TEST asc_he_44k_stereo(void)
{
    /* LC@22.05k core (idx 7), stereo, ext 44.1k (idx 4) */
    uint8_t buf[RSS_AAC_ASC_MAX];
    int len = rss_aac_asc(RSS_AAC_AOT_SBR, 44100, 2, buf);
    ASSERT_EQ(5, len);
    ASSERT_EQ(0x13, buf[0]);
    ASSERT_EQ(0x90, buf[1]);
    ASSERT_EQ(0x56, buf[2]);
    ASSERT_EQ(0xE5, buf[3]);
    ASSERT_EQ(0xA0, buf[4]);
    PASS();
}

TEST asc_rejects_bad_input(void)
{
    uint8_t buf[RSS_AAC_ASC_MAX];
    ASSERT_EQ(-1, rss_aac_asc(RSS_AAC_AOT_LC, 12345, 1, buf));
    ASSERT_EQ(-1, rss_aac_asc(RSS_AAC_AOT_SBR, 20000, 1, buf)); /* ext off-table */
    ASSERT_EQ(-1, rss_aac_asc(RSS_AAC_AOT_SBR, 7350, 1, buf));  /* core off-table */
    ASSERT_EQ(-1, rss_aac_asc(99, 48000, 1, buf));
    ASSERT_EQ(-1, rss_aac_asc(RSS_AAC_AOT_LC, 48000, 0, buf));
    ASSERT_EQ(-1, rss_aac_asc(RSS_AAC_AOT_LC, 48000, 1, NULL));
    PASS();
}

TEST rate_index_table(void)
{
    ASSERT_EQ(3, rss_aac_rate_index(48000));
    ASSERT_EQ(8, rss_aac_rate_index(16000));
    ASSERT_EQ(11, rss_aac_rate_index(8000));
    ASSERT_EQ(-1, rss_aac_rate_index(20000));
    PASS();
}

SUITE(aac_suite)
{
    RUN_TEST(asc_lc_16k_mono);
    RUN_TEST(asc_lc_48k_mono);
    RUN_TEST(asc_he_48k_mono);
    RUN_TEST(asc_he_44k_stereo);
    RUN_TEST(asc_rejects_bad_input);
    RUN_TEST(rate_index_table);
}
