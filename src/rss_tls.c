/*
 * rss_tls.c -- Shared TLS server for HTTPS endpoints
 */

#include "rss_tls.h"
#include "rss_common.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>

struct rss_tls_ctx {
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
};

struct rss_tls_conn {
    mbedtls_ssl_context ssl;
    int fd;
};

/* BIO callbacks for socket I/O */

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    ssize_t ret;
    do {
        ret = write(fd, buf, len);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)ret;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    ssize_t ret;
    do {
        ret = read(fd, buf, len);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (ret == 0)
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return (int)ret;
}

static int bio_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout)
{
    int fd = *(int *)ctx;
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int ret;
    do {
        ret = poll(&pfd, 1, timeout);
    } while (ret < 0 && errno == EINTR);
    if (ret <= 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    return bio_recv(ctx, buf, len);
}

rss_tls_ctx_t *rss_tls_init(const char *cert_path, const char *key_path)
{
    rss_tls_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    char errbuf[128];
    int ret;

    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_x509_crt_init(&ctx->cert);
    mbedtls_pk_init(&ctx->key);
    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                                (const unsigned char *)"rss_tls", 7);
    if (ret != 0)
        goto fail;

    ret = mbedtls_x509_crt_parse_file(&ctx->cert, cert_path);
    if (ret != 0) {
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        RSS_ERROR("TLS: failed to load cert %s: %s", cert_path, errbuf);
        goto fail;
    }

    ret = mbedtls_pk_parse_keyfile(&ctx->key, key_path, NULL, mbedtls_ctr_drbg_random,
                                   &ctx->ctr_drbg);
    if (ret != 0) {
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        RSS_ERROR("TLS: failed to load key %s: %s", key_path, errbuf);
        goto fail;
    }

    ret = mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_SERVER,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
        goto fail;

    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    ret = mbedtls_ssl_conf_own_cert(&ctx->conf, &ctx->cert, &ctx->key);
    if (ret != 0)
        goto fail;

    /* No client auth needed */
    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);

    RSS_INFO("TLS: HTTPS context initialized");
    return ctx;

fail:
    rss_tls_free(ctx);
    return NULL;
}

void rss_tls_free(rss_tls_ctx_t *ctx)
{
    if (!ctx)
        return;
    mbedtls_ssl_config_free(&ctx->conf);
    mbedtls_x509_crt_free(&ctx->cert);
    mbedtls_pk_free(&ctx->key);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
    free(ctx);
}

rss_tls_conn_t *rss_tls_accept(rss_tls_ctx_t *ctx, int fd, int timeout_ms)
{
    rss_tls_conn_t *conn = calloc(1, sizeof(*conn));
    if (!conn)
        return NULL;

    conn->fd = fd;
    mbedtls_ssl_init(&conn->ssl);

    /* Always set timeout (0 = no timeout). Must be set before ssl_setup
     * since mbedTLS copies the config. Unconditional to prevent stale
     * timeout from a previous connection leaking into the next one. */
    mbedtls_ssl_conf_read_timeout(&ctx->conf, timeout_ms);

    int ret = mbedtls_ssl_setup(&conn->ssl, &ctx->conf);
    if (ret != 0) {
        RSS_ERROR("TLS accept: ssl_setup failed: -0x%04x", (unsigned)-ret);
        free(conn);
        return NULL;
    }

    mbedtls_ssl_set_bio(&conn->ssl, &conn->fd, bio_send, bio_recv, bio_recv_timeout);

    /* Handshake loop — not a busy-wait because bio_recv_timeout uses poll()
     * and bio_send blocks on the blocking socket used by all callers. */
    while ((ret = mbedtls_ssl_handshake(&conn->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            RSS_DEBUG("TLS accept handshake failed: %s", errbuf);
            mbedtls_ssl_free(&conn->ssl);
            free(conn);
            return NULL;
        }
    }

    return conn;
}

void rss_tls_close(rss_tls_conn_t *conn)
{
    if (!conn)
        return;
    mbedtls_ssl_close_notify(&conn->ssl);
    mbedtls_ssl_free(&conn->ssl);
    free(conn);
}

ssize_t rss_tls_read(rss_tls_conn_t *conn, void *buf, size_t len)
{
    int ret = mbedtls_ssl_read(&conn->ssl, buf, len);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0)
        return 0;
    if (ret < 0)
        return -1;
    return ret;
}

ssize_t rss_tls_write(rss_tls_conn_t *conn, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t written = 0;

    while (written < len) {
        int ret = mbedtls_ssl_write(&conn->ssl, p + written, len - written);
        if (ret < 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
                /* Retry with same args (written not advanced) — satisfies
                 * mbedTLS requirement to retry WANT_* with identical pointer
                 * and length so buffered partial records can be flushed. */
                struct pollfd pfd = {
                    .fd = conn->fd,
                    .events = (ret == MBEDTLS_ERR_SSL_WANT_READ) ? POLLIN : POLLOUT,
                };
                poll(&pfd, 1, 100);
                continue;
            }
            return written > 0 ? (ssize_t)written : -1;
        }
        written += ret;
    }
    return (ssize_t)written;
}

int rss_tls_get_fd(rss_tls_conn_t *conn)
{
    return conn ? conn->fd : -1;
}

/* ── Client-side TLS ── */

struct rss_tls_client_ctx {
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
};

rss_tls_client_ctx_t *rss_tls_client_init(void)
{
    rss_tls_client_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    int ret;

    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_x509_crt_init(&ctx->cacert);
    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                                (const unsigned char *)"rss_tls_client", 14);
    if (ret != 0)
        goto fail;

    /* Load system CA certificates — required for server verification */
    ret = mbedtls_x509_crt_parse_path(&ctx->cacert, "/etc/ssl/certs");
    if (ret < 0)
        ret = mbedtls_x509_crt_parse_file(&ctx->cacert, "/etc/ssl/cert.pem");
    if (ctx->cacert.version == 0) {
        RSS_ERROR("TLS client: no CA certs loaded, cannot verify servers");
        goto fail;
    }

    ret = mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
        goto fail;

    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->cacert, NULL);
    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    RSS_INFO("TLS: client context initialized");
    return ctx;

fail:
    rss_tls_client_free(ctx);
    return NULL;
}

void rss_tls_client_free(rss_tls_client_ctx_t *ctx)
{
    if (!ctx)
        return;
    mbedtls_ssl_config_free(&ctx->conf);
    mbedtls_x509_crt_free(&ctx->cacert);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
    free(ctx);
}

rss_tls_conn_t *rss_tls_connect(rss_tls_client_ctx_t *ctx, int fd, const char *hostname,
                                int timeout_ms)
{
    if (!hostname || !hostname[0]) {
        RSS_ERROR("TLS connect: hostname required for certificate verification");
        return NULL;
    }

    rss_tls_conn_t *conn = calloc(1, sizeof(*conn));
    if (!conn)
        return NULL;

    conn->fd = fd;
    mbedtls_ssl_init(&conn->ssl);

    mbedtls_ssl_conf_read_timeout(&ctx->conf, timeout_ms);

    int ret = mbedtls_ssl_setup(&conn->ssl, &ctx->conf);
    if (ret != 0) {
        RSS_ERROR("TLS connect: ssl_setup failed: -0x%04x", (unsigned)-ret);
        free(conn);
        return NULL;
    }

    ret = mbedtls_ssl_set_hostname(&conn->ssl, hostname);
    if (ret != 0) {
        RSS_ERROR("TLS connect: set_hostname failed: %d", ret);
        mbedtls_ssl_free(&conn->ssl);
        free(conn);
        return NULL;
    }

    mbedtls_ssl_set_bio(&conn->ssl, &conn->fd, bio_send, bio_recv, bio_recv_timeout);

    while ((ret = mbedtls_ssl_handshake(&conn->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            RSS_ERROR("TLS client handshake failed: %s", errbuf);
            mbedtls_ssl_free(&conn->ssl);
            free(conn);
            return NULL;
        }
    }

    return conn;
}
