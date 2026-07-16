/*
 * rss_sign.h — device Ed25519 signing key
 *
 * One keypair per device, generated on first use from /dev/urandom
 * and persisted as a 32-byte seed. Shared by RMR (recording chains)
 * and RHD (signed snapshots). The public key is exported next to the
 * seed as <path>.pub (64 hex chars) for verifiers.
 */

#ifndef RSS_SIGN_H
#define RSS_SIGN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t secret[64]; /* monocypher ed25519 secret key */
    uint8_t public[32];
    uint8_t fingerprint[8]; /* SHA-512(public)[0..7] */
} rss_sign_key_t;

/*
 * Load the device key from key_path, generating and persisting a new
 * seed (plus the .pub hex export) when the file is missing.
 * Returns 0 on success, -1 on failure (never signs with a bad key).
 */
int rss_sign_key_load(rss_sign_key_t *key, const char *key_path);

#ifdef __cplusplus
}
#endif

#endif /* RSS_SIGN_H */
