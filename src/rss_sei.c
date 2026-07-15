/*
 * rss_sei.c — MISB ST 0604 Precision Time Stamp SEI builder/parser.
 *
 * Verified against ST 0604.6 §7 and GStreamer's parser
 * (gst_video_sei_user_data_unregistered_parse_precision_time_stamp).
 *
 * No emulation-prevention pass is needed: the identifiers contain no
 * zero bytes, the status byte always has bits 4-0 set, the 0xFF guard
 * bytes break any zero run inside the timestamp, and the payload ends
 * with the 0x80 rbsp stop bit — so three consecutive zero bytes can
 * never occur in the RBSP.
 */

#include "rss_sei.h"

#include <errno.h>
#include <string.h>

/* ST 0604.6 §7.1 Table 1 — H.264 Precision Time Stamp identifier */
static const uint8_t misp_uuid_h264[16] = "MISPmicrosectime";

/* ST 0604.6 §7.2 — H.265 Precision (microsecond) Time Stamp identifier */
static const uint8_t misp_uuid_h265[16] = {0xa8, 0x68, 0x7d, 0xd4, 0xd7, 0x59, 0x37, 0x58,
                                           0xa5, 0xce, 0xf0, 0x33, 0x8b, 0x65, 0x45, 0xf1};

#define SEI_PAYLOAD_TYPE_UDU 5 /* user_data_unregistered */
#define SEI_PAYLOAD_SIZE 28    /* 16 uuid + 1 status + 11 modified ts */

int rss_sei_build_timestamp(uint8_t *dst, size_t cap, int codec, int prefix, uint64_t utc_us,
                            uint8_t status)
{
    if (!dst)
        return -EINVAL;

    const uint8_t *uuid;
    size_t hdr_len;
    if (codec == RSS_SEI_CODEC_H264) {
        uuid = misp_uuid_h264;
        hdr_len = 1;
    } else if (codec == RSS_SEI_CODEC_H265) {
        uuid = misp_uuid_h265;
        hdr_len = 2;
    } else {
        return -EINVAL;
    }

    size_t prefix_len = (prefix == RSS_SEI_PREFIX_NONE) ? 0 : 4;
    /* header + payloadType + payloadSize + payload + rbsp trailing */
    size_t nal_len = hdr_len + 1 + 1 + SEI_PAYLOAD_SIZE + 1;
    size_t total = prefix_len + nal_len;
    if (cap < total)
        return -ENOSPC;

    uint8_t *p = dst;
    if (prefix == RSS_SEI_PREFIX_ANNEXB) {
        p[0] = 0x00;
        p[1] = 0x00;
        p[2] = 0x00;
        p[3] = 0x01;
        p += 4;
    } else if (prefix == RSS_SEI_PREFIX_AVCC) {
        p[0] = (uint8_t)(nal_len >> 24);
        p[1] = (uint8_t)(nal_len >> 16);
        p[2] = (uint8_t)(nal_len >> 8);
        p[3] = (uint8_t)nal_len;
        p += 4;
    } else if (prefix != RSS_SEI_PREFIX_NONE) {
        return -EINVAL;
    }

    if (codec == RSS_SEI_CODEC_H264) {
        *p++ = 0x06; /* nal_ref_idc=0, nal_unit_type=6 (SEI) */
    } else {
        *p++ = 0x4e; /* PREFIX_SEI_NUT(39), layer 0 */
        *p++ = 0x01; /* temporal_id_plus1 = 1 */
    }

    *p++ = SEI_PAYLOAD_TYPE_UDU;
    *p++ = SEI_PAYLOAD_SIZE;

    memcpy(p, uuid, 16);
    p += 16;

    /* Reserved bits 4-0 of the Time Status shall be 1 (ST 0603 §7.4).
     * This also guarantees the byte is non-zero. */
    *p++ = status | 0x1f;

    /* 8-byte big-endian value, 0xFF guard after each of the first
     * three 2-byte groups (ST 0604.6 §7.4 Table 2) */
    *p++ = (uint8_t)(utc_us >> 56);
    *p++ = (uint8_t)(utc_us >> 48);
    *p++ = 0xff;
    *p++ = (uint8_t)(utc_us >> 40);
    *p++ = (uint8_t)(utc_us >> 32);
    *p++ = 0xff;
    *p++ = (uint8_t)(utc_us >> 24);
    *p++ = (uint8_t)(utc_us >> 16);
    *p++ = 0xff;
    *p++ = (uint8_t)(utc_us >> 8);
    *p++ = (uint8_t)utc_us;

    *p++ = 0x80; /* rbsp_stop_one_bit */

    return (int)total;
}

int rss_sei_parse_timestamp(const uint8_t *nal, size_t len, int codec, uint64_t *utc_us,
                            uint8_t *status)
{
    if (!nal || !utc_us)
        return -EINVAL;

    const uint8_t *uuid;
    size_t hdr_len;
    if (codec == RSS_SEI_CODEC_H264) {
        if (len < 1 || (nal[0] & 0x1f) != 6)
            return -ENOENT;
        uuid = misp_uuid_h264;
        hdr_len = 1;
    } else if (codec == RSS_SEI_CODEC_H265) {
        if (len < 2 || ((nal[0] >> 1) & 0x3f) != 39)
            return -ENOENT;
        uuid = misp_uuid_h265;
        hdr_len = 2;
    } else {
        return -EINVAL;
    }

    /* Walk SEI messages in the NAL (a NAL may carry several).
     * Note: the RBSP is parsed unescaped. NALs built by rss_sei never
     * contain emulation-prevention bytes; foreign SEI messages that do
     * would shift the walk and read as absent, never as a false match
     * (the identifier comparison still applies). */
    const uint8_t *p = nal + hdr_len;
    const uint8_t *end = nal + len;

    while (p < end) {
        if (*p == 0x80) {
            /* rbsp trailing bits — only if nothing but zero
             * padding follows; 0x80 is otherwise a payloadType */
            const uint8_t *q = p + 1;
            while (q < end && *q == 0x00)
                q++;
            if (q == end)
                break;
        }

        uint32_t type = 0;
        while (p < end && *p == 0xff) {
            type += 0xff;
            p++;
        }
        if (p >= end)
            return -ENOENT;
        type += *p++;

        uint32_t size = 0;
        while (p < end && *p == 0xff) {
            size += 0xff;
            p++;
        }
        if (p >= end)
            return -ENOENT;
        size += *p++;

        if ((size_t)(end - p) < size)
            return -ENOENT;

        if (type == SEI_PAYLOAD_TYPE_UDU && size >= SEI_PAYLOAD_SIZE && memcmp(p, uuid, 16) == 0) {
            const uint8_t *d = p + 16;
            if (d[3] != 0xff || d[6] != 0xff || d[9] != 0xff)
                return -EBADMSG;
            if (status)
                *status = d[0];
            *utc_us = ((uint64_t)d[1] << 56) | ((uint64_t)d[2] << 48) | ((uint64_t)d[4] << 40) |
                      ((uint64_t)d[5] << 32) | ((uint64_t)d[7] << 24) | ((uint64_t)d[8] << 16) |
                      ((uint64_t)d[10] << 8) | (uint64_t)d[11];
            return 0;
        }

        p += size;
    }

    return -ENOENT;
}
