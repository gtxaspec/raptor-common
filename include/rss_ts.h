/*
 * rss_ts.h — MPEG-TS muxer for SRT/TS streaming
 *
 * Stateless, zero-allocation TS muxer. Writes PAT/PMT/PES into
 * caller-provided buffers. All output is a multiple of 188 bytes.
 */

#ifndef RSS_TS_H
#define RSS_TS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RSS_TS_PACKET_SIZE 188
#define RSS_TS_SYNC_BYTE 0x47
#define RSS_TS_PID_PAT 0x0000
#define RSS_TS_PID_PMT 0x1000
#define RSS_TS_PID_VIDEO 0x0100
#define RSS_TS_PID_AUDIO 0x0101

#define RSS_TS_STREAM_H264 0x1B
#define RSS_TS_STREAM_H265 0x24
#define RSS_TS_STREAM_AAC 0x0F
#define RSS_TS_STREAM_OPUS 0x06
#define RSS_TS_STREAM_NONE 0x00

typedef struct {
    uint8_t cc_pat;
    uint8_t cc_pmt;
    uint8_t cc_video;
    uint8_t cc_audio;
    uint32_t pat_counter;
    uint32_t pat_interval;
    uint8_t video_stream_type;
    uint8_t audio_stream_type;
} rss_ts_mux_t;

void rss_ts_init(rss_ts_mux_t *m, uint8_t video_type, uint8_t audio_type, uint32_t pat_interval);

size_t rss_ts_write_pat_pmt(rss_ts_mux_t *m, uint8_t *buf, size_t buf_size);

size_t rss_ts_write_video(rss_ts_mux_t *m, uint8_t *buf, size_t buf_size, const uint8_t *data,
                          size_t len, uint64_t pts_90khz, bool is_idr);

size_t rss_ts_write_audio(rss_ts_mux_t *m, uint8_t *buf, size_t buf_size, const uint8_t *data,
                          size_t len, uint64_t pts_90khz);

#ifdef __cplusplus
}
#endif

#endif /* RSS_TS_H */
