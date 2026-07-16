/*
 * rss_jpeg.c — JPEG capture-time EXIF and snapshot signing
 *
 * The EXIF APP1 is a fixed-layout little-endian TIFF with one IFD0
 * entry (the Exif IFD pointer) and three Exif IFD entries. The
 * signature APP15 sits immediately before EOI so the whole visible
 * image, EXIF included, is covered; verification anchors on the
 * fixed-size tail rather than walking entropy-coded data.
 */

#include "rss_jpeg.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <monocypher-ed25519.h>

/* Same identifier as the RMR signature boxes (rmr_sign.h) */
static const uint8_t sign_uuid[16] = {0xb0, 0x50, 0xbf, 0x07, 0x88, 0x56, 0x45, 0x98,
                                      0x86, 0x9e, 0x44, 0xc4, 0xdc, 0x20, 0x8f, 0x26};

#define SOI0 0xff
#define SOI1 0xd8
#define EOI0 0xff
#define EOI1 0xd9

/* TIFF layout offsets (relative to TIFF header start) */
#define TIFF_SIZE 102
#define EXIF_PAYLOAD (6 + TIFF_SIZE)    /* "Exif\0\0" + TIFF        */
#define EXIF_SEGMENT (4 + EXIF_PAYLOAD) /* marker + length + payload */

static void put16le(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

static void tiff_entry(uint8_t *p, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
{
    put16le(p, tag);
    put16le(p + 2, type);
    put32le(p + 4, count);
    put32le(p + 8, value);
}

int rss_jpeg_insert_exif(uint8_t *buf, size_t cap, size_t len, uint64_t utc_us)
{
    if (!buf || len < 4 || buf[0] != SOI0 || buf[1] != SOI1)
        return -EINVAL;
    if (cap < len + EXIF_SEGMENT)
        return -ENOSPC;

    /* Make room right after SOI */
    memmove(buf + 2 + EXIF_SEGMENT, buf + 2, len - 2);

    uint8_t *p = buf + 2;
    *p++ = 0xff;
    *p++ = 0xe1; /* APP1 */
    *p++ = ((EXIF_PAYLOAD + 2) >> 8) & 0xff;
    *p++ = (EXIF_PAYLOAD + 2) & 0xff;
    memcpy(p, "Exif\0\0", 6);
    p += 6;

    uint8_t *tiff = p;
    memcpy(tiff, "II", 2); /* little-endian */
    put16le(tiff + 2, 0x002a);
    put32le(tiff + 4, 8); /* IFD0 offset */

    /* IFD0: one entry — Exif IFD pointer */
    uint8_t *ifd0 = tiff + 8;
    put16le(ifd0, 1);
    tiff_entry(ifd0 + 2, 0x8769, 4 /*LONG*/, 1, 8 + 2 + 12 + 4); /* = 26 */
    put32le(ifd0 + 14, 0);                                       /* next IFD */

    /* Exif IFD: DateTimeOriginal, OffsetTimeOriginal, SubSecTimeOriginal */
    uint8_t *exif = tiff + 26;
    uint32_t val = 26 + 2 + 3 * 12 + 4; /* = 68, value area offset */
    put16le(exif, 3);
    tiff_entry(exif + 2, 0x9003, 2 /*ASCII*/, 20, val);
    tiff_entry(exif + 14, 0x9011, 2, 7, val + 20);
    tiff_entry(exif + 26, 0x9291, 2, 7, val + 27);
    put32le(exif + 38, 0); /* next IFD */

    time_t sec = (time_t)(utc_us / 1000000);
    struct tm tm;
    gmtime_r(&sec, &tm);
    char dt[21];
    strftime(dt, sizeof(dt), "%Y:%m:%d %H:%M:%S", &tm);
    char sub[8];
    snprintf(sub, sizeof(sub), "%06u", (unsigned)(utc_us % 1000000));

    uint8_t *values = tiff + val;
    memcpy(values, dt, 20);           /* 19 chars + NUL */
    memcpy(values + 20, "+00:00", 7); /* UTC declaration */
    memcpy(values + 27, sub, 7);      /* 6 digits + NUL  */

    return (int)(len + EXIF_SEGMENT);
}

int rss_jpeg_sign(uint8_t *buf, size_t cap, size_t len, const rss_sign_key_t *key)
{
    if (!buf || !key || len < 4 || buf[0] != SOI0 || buf[1] != SOI1)
        return -EINVAL;
    if (buf[len - 2] != EOI0 || buf[len - 1] != EOI1)
        return -EINVAL;
    if (cap < len + RSS_JPEG_SIG_SEGMENT)
        return -ENOSPC;

    size_t pos = len - 2; /* segment goes here, EOI moves back */

    uint8_t sig[64];
    crypto_ed25519_sign(sig, key->secret, buf, pos);

    buf[pos + RSS_JPEG_SIG_SEGMENT] = EOI0;
    buf[pos + RSS_JPEG_SIG_SEGMENT + 1] = EOI1;

    uint8_t *p = buf + pos;
    *p++ = 0xff;
    *p++ = 0xef;                                /* APP15 */
    uint16_t seglen = RSS_JPEG_SIG_SEGMENT - 2; /* excludes marker */
    *p++ = (seglen >> 8) & 0xff;
    *p++ = seglen & 0xff;
    memcpy(p, sign_uuid, 16);
    p += 16;
    *p++ = 1; /* version */
    memcpy(p, key->fingerprint, 8);
    p += 8;
    memcpy(p, sig, 64);

    return (int)(len + RSS_JPEG_SIG_SEGMENT);
}

int rss_jpeg_verify(const uint8_t *buf, size_t len, const uint8_t pub[32], uint8_t fp_out[8])
{
    if (!buf || len < 4)
        return -EINVAL;
    if (buf[0] != SOI0 || buf[1] != SOI1 || buf[len - 2] != EOI0 || buf[len - 1] != EOI1)
        return -EINVAL;
    if (len < (size_t)RSS_JPEG_SIG_SEGMENT + 4)
        return -ENOENT; /* valid JPEG, just too small to hold a signature */

    const uint8_t *seg = buf + len - 2 - RSS_JPEG_SIG_SEGMENT;
    if (seg[0] != 0xff || seg[1] != 0xef || memcmp(seg + 4, sign_uuid, 16) != 0)
        return -ENOENT;
    if (seg[20] != 1)
        return -EBADMSG;

    if (fp_out)
        memcpy(fp_out, seg + 21, 8);

    if (!pub)
        return 1; /* signature present, no key to verify against */

    const uint8_t *sig = seg + 29;
    if (crypto_ed25519_check(sig, pub, buf, (size_t)(seg - buf)) != 0)
        return -EBADMSG;
    return 0;
}

int rss_jpeg_get_exif_time(const uint8_t *buf, size_t len, uint64_t *utc_us)
{
    if (!buf || !utc_us || len < 4 || buf[0] != SOI0 || buf[1] != SOI1)
        return -EINVAL;

    /* Walk marker segments up to SOS looking for our APP1 layout */
    size_t pos = 2;
    while (pos + 4 <= len) {
        if (buf[pos] != 0xff)
            return -ENOENT;
        uint8_t marker = buf[pos + 1];
        if (marker == 0xda /* SOS */ || marker == 0xd9)
            return -ENOENT;
        size_t seglen = ((size_t)buf[pos + 2] << 8) | buf[pos + 3];
        if (seglen < 2 || pos + 2 + seglen > len)
            return -ENOENT;

        if (marker == 0xe1 && seglen >= 2 + EXIF_PAYLOAD &&
            memcmp(buf + pos + 4, "Exif\0\0", 6) == 0) {
            const uint8_t *tiff = buf + pos + 10;
            /* Fixed layout written by rss_jpeg_insert_exif. Copy the
             * fields out and NUL-terminate before parsing: the bytes
             * are untrusted (rverify reads arbitrary files) and sscanf
             * must never scan past them. */
            char dt[21];
            char sub[8];
            memcpy(dt, tiff + 68, 20);
            dt[20] = '\0';
            memcpy(sub, tiff + 95, 7);
            sub[7] = '\0';
            struct tm tm;
            memset(&tm, 0, sizeof(tm));
            if (sscanf(dt, "%4d:%2d:%2d %2d:%2d:%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
                return -ENOENT;
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            time_t sec = timegm(&tm);
            if (sec == (time_t)-1)
                return -ENOENT;
            unsigned frac = 0;
            sscanf(sub, "%6u", &frac);
            *utc_us = (uint64_t)sec * 1000000 + frac;
            return 0;
        }
        pos += 2 + seglen;
    }
    return -ENOENT;
}
