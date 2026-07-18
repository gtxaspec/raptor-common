/*
 * rss_aac.h -- AAC AudioSpecificConfig builder (ISO 14496-3)
 */
#ifndef RSS_AAC_H
#define RSS_AAC_H

#include <stdint.h>

#define RSS_AAC_AOT_LC 2
#define RSS_AAC_AOT_SBR 5 /* HE-AAC v1: SBR extension wrapping an LC core */

#define RSS_AAC_ASC_MAX 6

/*
 * Build the AudioSpecificConfig for an AAC stream.
 *
 * aot          RSS_AAC_AOT_LC or RSS_AAC_AOT_SBR
 * sample_rate  output rate in Hz; for SBR this is the extension (full)
 *              rate and the core runs at half of it
 * channels     channel count (channelConfiguration)
 * buf          receives the ASC, at least RSS_AAC_ASC_MAX bytes
 *
 * Returns the ASC length in bytes, or -1 if a rate has no entry in the
 * sampling-frequency table or the arguments are out of range.
 */
int rss_aac_asc(int aot, int sample_rate, int channels, uint8_t *buf);

/* Index into the ISO 14496-3 sampling-frequency table, or -1. */
int rss_aac_rate_index(int sample_rate);

#endif
