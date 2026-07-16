/*
 * rss_jpeg.h — JPEG capture-time EXIF and snapshot signing
 *
 * The JPEG counterparts of the H.26x SEI timecode and the RMR
 * signature chain:
 *
 * - rss_jpeg_insert_exif() splices a minimal EXIF APP1 right after
 *   SOI: DateTimeOriginal (UTC), OffsetTimeOriginal "+00:00", and
 *   SubSecTimeOriginal (microseconds). Every photo tool reads it.
 *
 * - rss_jpeg_sign() appends an APP15 signature segment immediately
 *   before EOI: the raptor signing UUID, version, key fingerprint,
 *   and an Ed25519 signature over every byte that precedes the
 *   segment (which includes the EXIF timestamp). The file stays a
 *   single well-formed JPEG; decoders ignore APP15.
 *
 * Both operate in place and return the new length.
 */

#ifndef RSS_JPEG_H
#define RSS_JPEG_H

#include <stddef.h>
#include <stdint.h>

#include "rss_sign.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RSS_JPEG_EXIF_MAX 128 /* headroom needed for insert_exif */
#define RSS_JPEG_SIG_SEGMENT 93 /* headroom needed for sign */

/*
 * Insert the EXIF APP1 after SOI. buf holds a complete JPEG of len
 * bytes with at least RSS_JPEG_EXIF_MAX spare capacity.
 * Returns the new length, or -EINVAL / -ENOSPC.
 */
int rss_jpeg_insert_exif(uint8_t *buf, size_t cap, size_t len, uint64_t utc_us);

/*
 * Sign the image: insert the APP15 signature segment before the
 * trailing EOI. Call after rss_jpeg_insert_exif so the timestamp is
 * covered. Returns the new length, or -EINVAL / -ENOSPC.
 */
int rss_jpeg_sign(uint8_t *buf, size_t cap, size_t len, const rss_sign_key_t *key);

/*
 * Verify a signed JPEG against a public key. fp_out (optional)
 * receives the embedded key fingerprint. Returns 0 when the
 * signature is valid, 1 when a signature is present but pub is NULL
 * (presence check only), -ENOENT when no signature segment is
 * present, -EBADMSG on structural or signature failure.
 */
int rss_jpeg_verify(const uint8_t *buf, size_t len, const uint8_t pub[32], uint8_t fp_out[8]);

/*
 * Extract the capture time written by rss_jpeg_insert_exif.
 * Returns 0 and fills utc_us, -ENOENT when absent/unparseable.
 */
int rss_jpeg_get_exif_time(const uint8_t *buf, size_t len, uint64_t *utc_us);

#ifdef __cplusplus
}
#endif

#endif /* RSS_JPEG_H */
