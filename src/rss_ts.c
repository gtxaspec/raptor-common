/*
 * rss_ts.c — MPEG-TS muxer (ISO 13818-1)
 *
 * Stateless, zero-allocation. Writes PAT/PMT/PES into caller buffers.
 * Designed for SRT live streaming (7 TS packets = 1316 bytes per send).
 */

#include "rss_ts.h"

#include <string.h>

/* ── CRC32 (MPEG polynomial 0x04C11DB7) ── */

static const uint32_t crc32_table[256] = {
    0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
    0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD,
    0x4C11DB70, 0x48D0C6C7, 0x4593E01E, 0x4152FDA9, 0x5F15ADAC, 0x5BD4B01B, 0x569796C2, 0x52568B75,
    0x6A1936C8, 0x6ED82B7F, 0x639B0DA6, 0x67FA1011, 0x79BD4014, 0x7D7C5DA3, 0x703F7B7A, 0x74FE66CD,
    0x9823B6E0, 0x9CE2AB57, 0x91A18D8E, 0x95609039, 0x8B27C03C, 0x8FE6DD8B, 0x82A5FB52, 0x8664E6E5,
    0xBE2B5B58, 0xBAEA46EF, 0xB7A96036, 0xB3687D81, 0xAD2F2D84, 0xA9EE3033, 0xA4AD16EA, 0xA06C0B5D,
    0xD4326D90, 0xD0F37027, 0xDDB056FE, 0xD9714B49, 0xC7361B4C, 0xC3F706FB, 0xCEB42022, 0xCA753D95,
    0xF23A8028, 0xF6FB9D9F, 0xFBB8BB46, 0xFF79A6F1, 0xE13EF6F4, 0xE5FFEB43, 0xE8BCCD9A, 0xEC7DD02D,
    0x34867077, 0x30476DC0, 0x3D044B19, 0x39C556AE, 0x278206AB, 0x23431B1C, 0x2E003DC5, 0x2AC12072,
    0x128E9DCF, 0x164F8078, 0x1B0CA6A1, 0x1FCDBB16, 0x018AEB13, 0x054BF6A4, 0x0808D07D, 0x0CC9CDCA,
    0x7897AB07, 0x7C56B6B0, 0x71159069, 0x75D48DDE, 0x6B93DDDB, 0x6F52C06C, 0x6211E6B5, 0x66D0FB02,
    0x5E9F46BF, 0x5A5E5B08, 0x571D7DD1, 0x53DC6066, 0x4D9B3063, 0x495A2DD4, 0x44190B0D, 0x40D816BA,
    0xACA5C697, 0xA864DB20, 0xA527FDF9, 0xA1E6E04E, 0xBFA1B04B, 0xBB60ADFC, 0xB6238B25, 0xB2E29692,
    0x8AAD2B2F, 0x8E6C3698, 0x832F1041, 0x87EE0DF6, 0x99A95DF3, 0x9D684044, 0x902B669D, 0x94EA7B2A,
    0xE0B41DE7, 0xE4750050, 0xE9362689, 0xEDF73B3E, 0xF3B06B3B, 0xF771768C, 0xFA325055, 0xFEF34DE2,
    0xC6BCF05F, 0xC27DEDE8, 0xCF3ECB31, 0xCBFFD686, 0xD5B88683, 0xD1799B34, 0xDC3ABDED, 0xD8FBA05A,
    0x690CE0EE, 0x6DCDFD59, 0x608EDB80, 0x644FC637, 0x7A089632, 0x7EC98B85, 0x738AAD5C, 0x774BB0EB,
    0x4F040D56, 0x4BC510E1, 0x46863638, 0x42472B8F, 0x5C007B8A, 0x58C1663D, 0x558240E4, 0x51435D53,
    0x251D3B9E, 0x21DC2629, 0x2C9F00F0, 0x285E1D47, 0x36194D42, 0x32D850F5, 0x3F9B762C, 0x3B5A6B9B,
    0x0315D626, 0x07D4CB91, 0x0A97ED48, 0x0E56F0FF, 0x1011A0FA, 0x14D0BD4D, 0x19939B94, 0x1D528623,
    0xF12F560E, 0xF5EE4BB9, 0xF8AD6D60, 0xFC6C70D7, 0xE22B20D2, 0xE6EA3D65, 0xEBA91BBC, 0xEF68060B,
    0xD727BBB6, 0xD3E6A601, 0xDEA580D8, 0xDA649D6F, 0xC423CD6A, 0xC0E2D0DD, 0xCDA1F604, 0xC960EBB3,
    0xBD3E8D7E, 0xB9FF90C9, 0xB4BCB610, 0xB07DABA7, 0xAE3AFBA2, 0xAAFBE615, 0xA7B8C0CC, 0xA379DD7B,
    0x9B3660C6, 0x9FF77D71, 0x92B45BA8, 0x9675461F, 0x8832161A, 0x8CF30BAD, 0x81B02D74, 0x857130C3,
    0x5D8A9099, 0x594B8D2E, 0x5408ABF7, 0x50C9B640, 0x4E8EE645, 0x4A4FFBF2, 0x470CDD2B, 0x43CDC09C,
    0x7B827D21, 0x7F436096, 0x7200464F, 0x76C15BF8, 0x68860BFD, 0x6C47164A, 0x61043093, 0x65C52D24,
    0x119B4BE9, 0x155A565E, 0x18197087, 0x1CD86D30, 0x029F3D35, 0x065E2082, 0x0B1D065B, 0x0FDC1BEC,
    0x3793A651, 0x3352BBE6, 0x3E119D3F, 0x3AD08088, 0x2497D08D, 0x2056CD3A, 0x2D15EBE3, 0x29D4F654,
    0xC5A92679, 0xC1683BCE, 0xCC2B1D17, 0xC8EA00A0, 0xD6AD50A5, 0xD26C4D12, 0xDF2F6BCB, 0xDBEE767C,
    0xE3A1CBC1, 0xE760D676, 0xEA23F0AF, 0xEEE2ED18, 0xF0A5BD1D, 0xF464A0AA, 0xF9278673, 0xFDE69BC4,
    0x89B8FD09, 0x8D79E0BE, 0x803AC667, 0x84FBDBD0, 0x9ABC8BD5, 0x9E7D9662, 0x933EB0BB, 0x97FFAD0C,
    0xAFB010B1, 0xAB710D06, 0xA6322BDF, 0xA2F33668, 0xBCB4666D, 0xB8757BDA, 0xB5365D03, 0xB1F740B4,
};

static uint32_t mpeg_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++)
        crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ data[i]) & 0xFF];
    return crc;
}

/* ── PTS/DTS encoding (5 bytes, 33-bit timestamp) ── */

static void write_pts(uint8_t *p, uint8_t prefix, uint64_t pts)
{
    p[0] = (uint8_t)((prefix << 4) | (((pts >> 30) & 0x07) << 1) | 1);
    p[1] = (uint8_t)((pts >> 22) & 0xFF);
    p[2] = (uint8_t)((((pts >> 15) & 0x7F) << 1) | 1);
    p[3] = (uint8_t)((pts >> 7) & 0xFF);
    p[4] = (uint8_t)((((pts) & 0x7F) << 1) | 1);
}

/* ── PCR encoding (6 bytes) ── */

static void write_pcr(uint8_t *p, uint64_t pcr_base)
{
    p[0] = (uint8_t)(pcr_base >> 25);
    p[1] = (uint8_t)(pcr_base >> 17);
    p[2] = (uint8_t)(pcr_base >> 9);
    p[3] = (uint8_t)(pcr_base >> 1);
    p[4] = (uint8_t)(((pcr_base & 1) << 7) | 0x7E);
    p[5] = 0;
}

/* ── TS packet helpers ── */

static uint8_t *start_ts_packet(uint8_t *p, uint16_t pid, bool pusi, uint8_t *cc)
{
    p[0] = RSS_TS_SYNC_BYTE;
    p[1] = (uint8_t)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    p[2] = (uint8_t)(pid & 0xFF);
    p[3] = (uint8_t)(0x10 | (*cc & 0x0F));
    *cc = (*cc + 1) & 0x0F;
    return p + 4;
}

static size_t write_pes_packets(uint8_t *buf, size_t buf_size, uint16_t pid, uint8_t *cc,
                                const uint8_t *pes_hdr, size_t pes_hdr_len, const uint8_t *payload,
                                size_t payload_len, uint64_t pcr_90khz, bool has_pcr, bool is_rai)
{
    size_t total_data = pes_hdr_len + payload_len;
    size_t written = 0;
    size_t data_off = 0;
    bool first = true;

    while (data_off < total_data) {
        if (written + RSS_TS_PACKET_SIZE > buf_size)
            break;

        uint8_t *pkt = buf + written;
        memset(pkt, 0xFF, RSS_TS_PACKET_SIZE);

        pkt[0] = RSS_TS_SYNC_BYTE;
        pkt[1] = (uint8_t)((first ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
        pkt[2] = (uint8_t)(pid & 0xFF);

        size_t hdr_size = 4;
        size_t adapt_size = 0;
        size_t remaining = total_data - data_off;

        /* Adaptation field for first packet (PCR, RAI) or stuffing */
        if (first && (has_pcr || is_rai)) {
            uint8_t af_flags = 0;
            size_t af_data = 0;

            if (has_pcr) {
                af_flags |= 0x10;
                af_data += 6;
            }
            if (is_rai)
                af_flags |= 0x40;

            adapt_size = 2 + af_data;
            pkt[4] = (uint8_t)(1 + af_data);
            pkt[5] = af_flags;
            if (has_pcr)
                write_pcr(pkt + 6, pcr_90khz);

            hdr_size += adapt_size;
        }

        size_t payload_space = RSS_TS_PACKET_SIZE - hdr_size;

        /* Need stuffing if this is the last packet and data < space */
        if (remaining < payload_space) {
            size_t stuff = payload_space - remaining;

            if (adapt_size > 0) {
                /* Extend existing adaptation field (packet is pre-filled 0xFF) */
                pkt[4] += (uint8_t)stuff;
            } else if (stuff == 1) {
                /* Single byte: adaptation_field_length = 0 */
                pkt[3] = (uint8_t)(0x30 | (*cc & 0x0F));
                pkt[4] = 0;
                hdr_size = 5;
            } else {
                /* Multi-byte adaptation field for stuffing */
                pkt[3] = (uint8_t)(0x30 | (*cc & 0x0F));
                pkt[4] = (uint8_t)(stuff - 1);
                pkt[5] = 0;
                if (stuff > 2)
                    memset(pkt + 6, 0xFF, stuff - 2);
                hdr_size = 4 + stuff;
            }
            payload_space = remaining;
            /* CC already set above for stuff cases */
            if (adapt_size > 0)
                pkt[3] = (uint8_t)(0x30 | (*cc & 0x0F));
        } else {
            /* Payload only (or adaptation + payload already set) */
            if (adapt_size > 0)
                pkt[3] = (uint8_t)(0x30 | (*cc & 0x0F));
            else
                pkt[3] = (uint8_t)(0x10 | (*cc & 0x0F));
        }
        *cc = (*cc + 1) & 0x0F;

        /* Copy data: first from PES header, then from payload */
        size_t pkt_off = RSS_TS_PACKET_SIZE - payload_space;
        size_t to_copy = payload_space;

        while (to_copy > 0 && data_off < total_data) {
            size_t chunk;

            if (data_off < pes_hdr_len) {
                chunk = pes_hdr_len - data_off;
                if (chunk > to_copy)
                    chunk = to_copy;
                memcpy(pkt + pkt_off, pes_hdr + data_off, chunk);
            } else {
                size_t pl_off = data_off - pes_hdr_len;

                chunk = payload_len - pl_off;
                if (chunk > to_copy)
                    chunk = to_copy;
                memcpy(pkt + pkt_off, payload + pl_off, chunk);
            }
            pkt_off += chunk;
            data_off += chunk;
            to_copy -= chunk;
        }

        written += RSS_TS_PACKET_SIZE;
        first = false;
    }

    return written;
}

/* ── Public API ── */

void rss_ts_init(rss_ts_mux_t *m, uint8_t video_type, uint8_t audio_type, uint32_t pat_interval)
{
    memset(m, 0, sizeof(*m));
    m->video_stream_type = video_type;
    m->audio_stream_type = audio_type;
    m->pat_interval = pat_interval;
}

size_t rss_ts_write_pat_pmt(rss_ts_mux_t *m, uint8_t *buf, size_t buf_size)
{
    if (buf_size < RSS_TS_PACKET_SIZE * 2)
        return 0;

    size_t written = 0;

    /* ── PAT ── */
    uint8_t *pkt = buf;

    memset(pkt, 0xFF, RSS_TS_PACKET_SIZE);
    start_ts_packet(pkt, RSS_TS_PID_PAT, true, &m->cc_pat);

    uint8_t *p = pkt + 4;

    *p++ = 0x00; /* pointer_field */

    uint8_t *section = p;

    *p++ = 0x00; /* table_id: PAT */
    *p++ = 0xB0; /* section_syntax_indicator=1, length high */
    *p++ = 0x0D; /* section length = 13 */
    *p++ = 0x00;
    *p++ = 0x01; /* transport_stream_id = 1 */
    *p++ = 0xC1; /* version=0, current_next=1 */
    *p++ = 0x00; /* section_number */
    *p++ = 0x00; /* last_section_number */
    *p++ = 0x00;
    *p++ = 0x01; /* program_number = 1 */
    *p++ = (uint8_t)(0xE0 | ((RSS_TS_PID_PMT >> 8) & 0x1F));
    *p++ = (uint8_t)(RSS_TS_PID_PMT & 0xFF);

    uint32_t crc = mpeg_crc32(section, (size_t)(p - section));

    *p++ = (uint8_t)(crc >> 24);
    *p++ = (uint8_t)(crc >> 16);
    *p++ = (uint8_t)(crc >> 8);
    *p++ = (uint8_t)(crc);

    written += RSS_TS_PACKET_SIZE;

    /* ── PMT ── */
    pkt = buf + written;
    memset(pkt, 0xFF, RSS_TS_PACKET_SIZE);
    start_ts_packet(pkt, RSS_TS_PID_PMT, true, &m->cc_pmt);

    p = pkt + 4;
    *p++ = 0x00; /* pointer_field */

    section = p;
    *p++ = 0x02; /* table_id: PMT */

    uint8_t *len_pos = p;

    p += 2; /* skip section_length, fill later */

    *p++ = 0x00;
    *p++ = 0x01; /* program_number = 1 */
    *p++ = 0xC1; /* version=0, current_next=1 */
    *p++ = 0x00; /* section_number */
    *p++ = 0x00; /* last_section_number */

    /* PCR on video PID */
    *p++ = (uint8_t)(0xE0 | ((RSS_TS_PID_VIDEO >> 8) & 0x1F));
    *p++ = (uint8_t)(RSS_TS_PID_VIDEO & 0xFF);

    *p++ = 0xF0;
    *p++ = 0x00; /* program_info_length = 0 */

    /* Video ES */
    *p++ = m->video_stream_type;
    *p++ = (uint8_t)(0xE0 | ((RSS_TS_PID_VIDEO >> 8) & 0x1F));
    *p++ = (uint8_t)(RSS_TS_PID_VIDEO & 0xFF);
    *p++ = 0xF0;
    *p++ = 0x00; /* ES_info_length = 0 */

    /* Audio ES (if configured) */
    if (m->audio_stream_type != RSS_TS_STREAM_NONE) {
        /* Map internal audio hints to ISO 13818-1 stream_type */
        uint8_t ts_audio_type = m->audio_stream_type;

        if (ts_audio_type >= 0x80)
            ts_audio_type = 0x06; /* private data (§2.4.4.9) */

        *p++ = ts_audio_type;
        *p++ = (uint8_t)(0xE0 | ((RSS_TS_PID_AUDIO >> 8) & 0x1F));
        *p++ = (uint8_t)(RSS_TS_PID_AUDIO & 0xFF);

        if (m->audio_stream_type == RSS_TS_STREAM_OPUS) {
            /* §2.6.1 registration_descriptor */
            *p++ = 0xF0;
            *p++ = 0x06;
            *p++ = 0x05; /* descriptor_tag */
            *p++ = 0x04; /* descriptor_length */
            *p++ = 'O';
            *p++ = 'p';
            *p++ = 'u';
            *p++ = 's';
        } else if (m->audio_stream_type == RSS_TS_STREAM_PCMU ||
                   m->audio_stream_type == RSS_TS_STREAM_PCMA ||
                   m->audio_stream_type == RSS_TS_STREAM_L16) {
            /* §2.6.1 registration_descriptor for raw audio */
            *p++ = 0xF0;
            *p++ = 0x06;
            *p++ = 0x05; /* descriptor_tag */
            *p++ = 0x04; /* descriptor_length */
            *p++ = 'L';
            *p++ = 'P';
            *p++ = 'C';
            *p++ = 'M';
        } else {
            *p++ = 0xF0;
            *p++ = 0x00;
        }
    }

    /* Fill section length (includes from program_number to end of CRC) */
    size_t section_len = (size_t)(p - len_pos - 2) + 4;

    len_pos[0] = (uint8_t)(0xB0 | ((section_len >> 8) & 0x0F));
    len_pos[1] = (uint8_t)(section_len & 0xFF);

    crc = mpeg_crc32(section, (size_t)(p - section));
    *p++ = (uint8_t)(crc >> 24);
    *p++ = (uint8_t)(crc >> 16);
    *p++ = (uint8_t)(crc >> 8);
    *p++ = (uint8_t)(crc);

    written += RSS_TS_PACKET_SIZE;
    m->pat_counter = 0;

    return written;
}

size_t rss_ts_write_video(rss_ts_mux_t *m, uint8_t *buf, size_t buf_size, const uint8_t *data,
                          size_t len, uint64_t pts_90khz, uint64_t dts_90khz, bool is_idr)
{
    if (len == 0)
        return 0;

    bool has_dts = (dts_90khz != pts_90khz);
    /* PES header: 9 base + 5 PTS + optional 5 DTS */
    uint8_t pes_hdr[19];
    size_t pes_hdr_len;

    pes_hdr[0] = 0x00;
    pes_hdr[1] = 0x00;
    pes_hdr[2] = 0x01;
    pes_hdr[3] = 0xE0; /* stream_id: video */
    pes_hdr[4] = 0x00;
    pes_hdr[5] = 0x00; /* PES length = 0 (unbounded, ISO 13818-1 §2.4.3.7) */
    pes_hdr[6] = 0x84; /* '10' marker, data_alignment_indicator=1 (§2.4.3.7) */

    if (has_dts) {
        pes_hdr[7] = 0xC0; /* PTS+DTS present (§2.4.3.7 PTS_DTS_flags='11') */
        pes_hdr[8] = 0x0A; /* PES header data length = 10 */
        write_pts(pes_hdr + 9, 0x03, pts_90khz);
        write_pts(pes_hdr + 14, 0x01, dts_90khz);
        pes_hdr_len = 19;
    } else {
        pes_hdr[7] = 0x80; /* PTS only (§2.4.3.7 PTS_DTS_flags='10') */
        pes_hdr[8] = 0x05; /* PES header data length = 5 */
        write_pts(pes_hdr + 9, 0x02, pts_90khz);
        pes_hdr_len = 14;
    }

    /* PCR derived from DTS (§2.4.4.2: PCR ≤ DTS) */
    uint64_t pcr = dts_90khz > 3000 ? dts_90khz - 3000 : 0;

    size_t written = write_pes_packets(buf, buf_size, RSS_TS_PID_VIDEO, &m->cc_video, pes_hdr,
                                       pes_hdr_len, data, len, pcr, true, is_idr);

    m->pat_counter++;
    return written;
}

size_t rss_ts_write_audio(rss_ts_mux_t *m, uint8_t *buf, size_t buf_size, const uint8_t *data,
                          size_t len, uint64_t pts_90khz)
{
    if (len == 0 || m->audio_stream_type == RSS_TS_STREAM_NONE)
        return 0;

    /* §2.4.3.13: 0xC0 = ISO 13818-3 audio, 0xBD = private_stream_1 */
    uint8_t stream_id = (m->audio_stream_type == RSS_TS_STREAM_AAC) ? 0xC0 : 0xBD;
    uint8_t pes_hdr[14];

    pes_hdr[0] = 0x00;
    pes_hdr[1] = 0x00;
    pes_hdr[2] = 0x01;
    pes_hdr[3] = stream_id;

    /* Audio PES can have bounded length */
    size_t pes_payload = 3 + 5 + len; /* flags(3) + pts(5) + data */

    if (pes_payload <= 0xFFFF) {
        pes_hdr[4] = (uint8_t)((pes_payload >> 8) & 0xFF);
        pes_hdr[5] = (uint8_t)(pes_payload & 0xFF);
    } else {
        pes_hdr[4] = 0x00;
        pes_hdr[5] = 0x00;
    }

    pes_hdr[6] = 0x84; /* '10' marker, data_alignment_indicator=1 */
    pes_hdr[7] = 0x80; /* PTS only */
    pes_hdr[8] = 0x05;
    write_pts(pes_hdr + 9, 0x02, pts_90khz);

    return write_pes_packets(buf, buf_size, RSS_TS_PID_AUDIO, &m->cc_audio, pes_hdr,
                             sizeof(pes_hdr), data, len, 0, false, false);
}
