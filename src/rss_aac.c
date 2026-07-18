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
        /* Backward-compatible explicit signaling (ISO 14496-3 1.6.6): a
         * plain LC ASC at the core rate followed by a syncExtension
         * announcing SBR and the extension rate. Legacy LC decoders play
         * the core; SBR decoders upsample. Matches the form faac itself
         * emits, so SDP/esds signaling is byte-identical to the
         * encoder's own ASC.
         *
         * AOT(5)=LC freqIdx(4)=core chanCfg(4) GASpecific(3)
         * syncExtensionType(11)=0x2b7 AOT(5)=SBR sbrPresent(1)=1
         * extFreqIdx(4) = 37 bits, zero-padded to 5 bytes */
        uint64_t v = 0;
        v |= (uint64_t)RSS_AAC_AOT_LC << 35;
        v |= (uint64_t)core_idx << 31;
        v |= (uint64_t)channels << 27;
        /* GASpecificConfig: 000 */
        v |= (uint64_t)0x2b7 << 13;
        v |= (uint64_t)RSS_AAC_AOT_SBR << 8;
        v |= (uint64_t)1 << 7;
        v |= (uint64_t)ext_idx << 3;
        buf[0] = (uint8_t)(v >> 32);
        buf[1] = (uint8_t)(v >> 24);
        buf[2] = (uint8_t)(v >> 16);
        buf[3] = (uint8_t)(v >> 8);
        buf[4] = (uint8_t)(v & 0xFF);
        return 5;
    }

    return -1;
}
