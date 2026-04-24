/*
 * rss_tls.h -- Shared TLS for HTTPS endpoints and client connections
 *
 * Simple wrapper around mbedTLS for TCP stream TLS.
 * Server: RHD (HTTPS snapshots/MJPEG), RWD (HTTPS WHIP signaling).
 * Client: RSP (RTMPS push to YouTube/Twitch/etc).
 *
 * Requires mbedTLS in the sysroot. Guard usage with RSS_HAS_TLS.
 */

#ifndef RSS_TLS_H
#define RSS_TLS_H

#include <stddef.h>
#include <sys/types.h>

typedef struct rss_tls_ctx rss_tls_ctx_t;
typedef struct rss_tls_conn rss_tls_conn_t;

/* Client-side TLS context (no cert/key needed). */
typedef struct rss_tls_client_ctx rss_tls_client_ctx_t;

/*
 * Initialize a TLS server context with the given certificate and key.
 * Returns context on success, NULL on error (logged internally).
 */
rss_tls_ctx_t *rss_tls_init(const char *cert_path, const char *key_path);

/*
 * Free the TLS server context.
 */
void rss_tls_free(rss_tls_ctx_t *ctx);

/*
 * Perform TLS handshake on an accepted TCP socket.
 * Returns a TLS connection on success, NULL on failure (fd is NOT closed).
 * Timeout in milliseconds for the handshake (0 = blocking).
 */
rss_tls_conn_t *rss_tls_accept(rss_tls_ctx_t *ctx, int fd, int timeout_ms);

/*
 * Initialize a TLS client context for outgoing connections.
 * No certificate needed — validates server cert against system CA bundle.
 */
rss_tls_client_ctx_t *rss_tls_client_init(void);

/*
 * Free the TLS client context.
 */
void rss_tls_client_free(rss_tls_client_ctx_t *ctx);

/*
 * Perform client-side TLS handshake on a connected TCP socket.
 * hostname is used for SNI. Returns a TLS connection on success,
 * NULL on failure (fd is NOT closed).
 */
rss_tls_conn_t *rss_tls_connect(rss_tls_client_ctx_t *ctx, int fd,
                                const char *hostname, int timeout_ms);

/*
 * Close and free a TLS connection. Does NOT close the underlying fd.
 */
void rss_tls_close(rss_tls_conn_t *conn);

/*
 * Read from TLS connection. Returns bytes read, 0 on EOF, -1 on error.
 */
ssize_t rss_tls_read(rss_tls_conn_t *conn, void *buf, size_t len);

/*
 * Write to TLS connection. Returns bytes written, -1 on error.
 */
ssize_t rss_tls_write(rss_tls_conn_t *conn, const void *buf, size_t len);

/*
 * Get the underlying fd (for epoll/poll registration).
 */
int rss_tls_get_fd(rss_tls_conn_t *conn);

#endif /* RSS_TLS_H */
