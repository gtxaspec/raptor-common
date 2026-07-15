/*
 * rss_sei.h — MISB ST 0604 Precision Time Stamp SEI (H.264/H.265)
 *
 * Builds and parses the user_data_unregistered SEI message (payload
 * type 5) carrying the per-frame UTC capture time: 16-byte identifier,
 * 1-byte MISB ST 0603 Time Status, and the 8-byte big-endian
 * microsecond timestamp as four 2-byte groups with a 0xFF guard byte
 * after each of the first three (ST 0604.6 §7.4 Table 2).
 *
 * The guard bytes make start-code emulation impossible by
 * construction, so the NAL needs no emulation-prevention pass.
 */

#ifndef RSS_SEI_H
#define RSS_SEI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Codec ids match rss_codec_t (raptor_hal.h) and ring header codec */
#define RSS_SEI_CODEC_H264 0
#define RSS_SEI_CODEC_H265 1

/* Output framing */
#define RSS_SEI_PREFIX_NONE 0   /* bare NAL                     */
#define RSS_SEI_PREFIX_ANNEXB 1 /* 4-byte 00 00 00 01 start code */
#define RSS_SEI_PREFIX_AVCC 2   /* 4-byte big-endian NAL length  */

/* MISB ST 0603 §7.4 Time Status byte. Bit 7: 0 = locked to absolute
 * time reference, 1 = lock unknown. Bit 6: discontinuity. Bit 5:
 * reverse jump. Bits 4-0: reserved, always 1. Same values as
 * RSS_UTC_STATUS_* in rss_ipc.h. */
#define RSS_SEI_TS_LOCKED 0x1F
#define RSS_SEI_TS_UNLOCKED 0x9F

/* Largest possible output: H.265 NAL (33 bytes) + 4-byte prefix */
#define RSS_SEI_TS_MAX 37

/*
 * Build a Precision Time Stamp SEI NAL unit.
 *
 * dst/cap:  output buffer (RSS_SEI_TS_MAX is always enough)
 * codec:    RSS_SEI_CODEC_H264 / RSS_SEI_CODEC_H265
 * prefix:   RSS_SEI_PREFIX_NONE / ANNEXB / AVCC
 * utc_us:   microseconds since the Unix epoch (UTC)
 * status:   RSS_SEI_TS_LOCKED / RSS_SEI_TS_UNLOCKED. Reserved bits
 *           4-0 are forced to 1 per ST 0603 regardless of input.
 *
 * Returns total bytes written, or -EINVAL / -ENOSPC.
 */
int rss_sei_build_timestamp(uint8_t *dst, size_t cap, int codec, int prefix, uint64_t utc_us,
                            uint8_t status);

/*
 * Parse a bare NAL unit (no start code / length prefix).
 *
 * Returns 0 and fills utc_us (and status if non-NULL) when the NAL is
 * a Precision Time Stamp SEI for the given codec. Returns -ENOENT if
 * it is some other NAL or SEI, -EBADMSG if it matches but is
 * malformed (truncated, bad guard byte).
 */
int rss_sei_parse_timestamp(const uint8_t *nal, size_t len, int codec, uint64_t *utc_us,
                            uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* RSS_SEI_H */
