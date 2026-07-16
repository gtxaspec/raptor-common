/*
 * rss_sign.c — device Ed25519 signing key (moved from rmr_sign.c so
 * RHD snapshot signing shares the same key and generation logic).
 */

#include "rss_sign.h"
#include "rss_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <monocypher.h>
#include <monocypher-ed25519.h>

static int read_urandom(uint8_t *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    close(fd);
    return 0;
}

static int write_file_mode(const char *path, const void *buf, size_t len, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -1;
    ssize_t n = write(fd, buf, len);
    int ret = (n == (ssize_t)len && fsync(fd) == 0) ? 0 : -1;
    close(fd);
    return ret;
}

static void mkdir_parent(const char *path)
{
    char dir[256];
    rss_strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        mkdir(dir, 0755);
    }
}

int rss_sign_key_load(rss_sign_key_t *key, const char *key_path)
{
    uint8_t seed[32];

    /*
     * Several daemons (RMR, RHD) load the same key at boot; O_EXCL
     * makes first-boot generation race-safe — the loser re-reads the
     * winner's seed (a 32-byte single write, so no torn reads).
     */
    for (int attempt = 0;; attempt++) {
        int fd = open(key_path, O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, seed, sizeof(seed));
            close(fd);
            if (n == (ssize_t)sizeof(seed))
                break;
            if (attempt < 3) {
                usleep(10000);
                continue;
            }
            RSS_ERROR("sign key %s: short read (%zd), refusing to sign", key_path, n);
            return -1;
        }

        if (read_urandom(seed, sizeof(seed)) < 0) {
            RSS_ERROR("sign key generation: /dev/urandom unavailable");
            return -1;
        }
        mkdir_parent(key_path);
        fd = open(key_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            if (errno == EEXIST && attempt < 3)
                continue; /* another daemon won the race */
            RSS_ERROR("sign key %s: persist failed: %s", key_path, strerror(errno));
            crypto_wipe(seed, sizeof(seed));
            return -1;
        }
        ssize_t n = write(fd, seed, sizeof(seed));
        int ok = (n == (ssize_t)sizeof(seed) && fsync(fd) == 0);
        close(fd);
        if (!ok) {
            RSS_ERROR("sign key %s: persist failed: %s", key_path, strerror(errno));
            unlink(key_path);
            crypto_wipe(seed, sizeof(seed));
            return -1;
        }
        RSS_INFO("generated new signing key: %s", key_path);
        break;
    }

    crypto_ed25519_key_pair(key->secret, key->public, seed);
    crypto_wipe(seed, sizeof(seed));

    uint8_t digest[64];
    crypto_sha512(digest, key->public, sizeof(key->public));
    memcpy(key->fingerprint, digest, sizeof(key->fingerprint));
    crypto_wipe(digest, sizeof(digest));

    /* Convenience hex export of the public key for verifiers */
    char pub_path[256];
    snprintf(pub_path, sizeof(pub_path), "%s.pub", key_path);
    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", key->public[i]);
    if (write_file_mode(pub_path, hex, 64, 0644) < 0)
        RSS_WARN("pubkey export %s failed: %s", pub_path, strerror(errno));

    RSS_INFO("signing key loaded, fingerprint %02x%02x%02x%02x%02x%02x%02x%02x",
             key->fingerprint[0], key->fingerprint[1], key->fingerprint[2], key->fingerprint[3],
             key->fingerprint[4], key->fingerprint[5], key->fingerprint[6], key->fingerprint[7]);
    return 0;
}
