/*
 * rss_aac.c -- AAC AudioSpecificConfig builder (ISO 14496-3)
 *
 * Emits the two shapes RSS produces: plain AAC-LC, and HE-AAC v1 with
 * explicit hierarchical SBR signaling (AOT 5 wrapping an LC core at
 * half the extension rate). Explicit signaling is used so receivers do
 * not have to sniff SBR from the payload.
 */

#include <rss_aac.h>

static const int sr_table[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                               22050, 16000, 12000, 11025, 8000,  7350};

int rss_aac_rate_index(int sample_rate)
{
    for (int i = 0; i < (int)(sizeof(sr_table) / sizeof(sr_table[0])); i++) {
        if (sr_table[i] == sample_rate)
            return i;
    }
    return -1;
}

int rss_aac_asc(int aot, int sample_rate, int channels, uint8_t *buf)
{
    if (!buf || channels < 1 || channels > 7)
        return -1;

    if (aot == RSS_AAC_AOT_LC) {
        int idx = rss_aac_rate_index(sample_rate);
        if (idx < 0)
            return -1;
        /* AOT(5) freqIdx(4) chanCfg(4) GASpecificConfig(3) = 16 bits */
        uint16_t v = (uint16_t)((RSS_AAC_AOT_LC << 11) | (idx << 7) | (channels << 3));
        buf[0] = (uint8_t)(v >> 8);
        buf[1] = (uint8_t)(v & 0xFF);
        return 2;
    }

    if (aot == RSS_AAC_AOT_SBR) {
        int core_idx = rss_aac_rate_index(sample_rate / 2);
        int ext_idx = rss_aac_rate_index(sample_rate);
        if (core_idx < 0 || ext_idx < 0)
            return -1;
        /* AOT(5)=SBR freqIdx(4)=core chanCfg(4) extFreqIdx(4)
         * AOT(5)=LC GASpecificConfig(3) = 25 bits, zero-padded */
        uint32_t v = 0;
        v |= (uint32_t)RSS_AAC_AOT_SBR << 27;
        v |= (uint32_t)core_idx << 23;
        v |= (uint32_t)channels << 19;
        v |= (uint32_t)ext_idx << 15;
        v |= (uint32_t)RSS_AAC_AOT_LC << 10;
        buf[0] = (uint8_t)(v >> 24);
        buf[1] = (uint8_t)(v >> 16);
        buf[2] = (uint8_t)(v >> 8);
        buf[3] = (uint8_t)(v & 0xFF);
        return 4;
    }

    return -1;
}
