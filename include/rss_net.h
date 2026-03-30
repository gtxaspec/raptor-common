/*
 * rss_net.h — Network utility helpers for RSS daemons
 *
 * Address formatting, socket setup, listen socket creation.
 * All static inline — no additional .c file needed.
 */

#ifndef RSS_NET_H
#define RSS_NET_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Format sockaddr_storage to string. IPv4-mapped IPv6 shown as plain IPv4. */
static inline const char *rss_addr_str(const struct sockaddr_storage *ss,
				       char *buf, size_t bufsz)
{
	if (ss->ss_family == AF_INET) {
		inet_ntop(AF_INET, &((const struct sockaddr_in *)ss)->sin_addr,
			  buf, bufsz);
	} else if (ss->ss_family == AF_INET6) {
		const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)ss;
		if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr))
			inet_ntop(AF_INET, &s6->sin6_addr.s6_addr[12],
				  buf, bufsz);
		else
			inet_ntop(AF_INET6, &s6->sin6_addr, buf, bufsz);
	} else {
		snprintf(buf, bufsz, "???");
	}
	return buf;
}

/* Extract port from sockaddr_storage (host byte order). */
static inline uint16_t rss_addr_port(const struct sockaddr_storage *ss)
{
	if (ss->ss_family == AF_INET)
		return ntohs(((const struct sockaddr_in *)ss)->sin_port);
	if (ss->ss_family == AF_INET6)
		return ntohs(((const struct sockaddr_in6 *)ss)->sin6_port);
	return 0;
}

/* Set fd to non-blocking mode. Returns 0 on success, -1 on error. */
static inline int rss_set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * Create a dual-stack TCP listen socket on the given port.
 * Sets SO_REUSEADDR, disables IPV6_V6ONLY, binds to in6addr_any.
 * Returns fd on success, -1 on error.
 */
static inline int rss_listen_tcp(int port, int backlog)
{
	int fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	int zero = 0;
	setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));

	struct sockaddr_in6 addr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons((uint16_t)port),
		.sin6_addr = IN6ADDR_ANY_INIT,
	};

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	if (listen(fd, backlog) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/* Set TCP_NODELAY on a socket. */
static inline void rss_set_tcp_nodelay(int fd)
{
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

#ifdef __cplusplus
}
#endif

#endif /* RSS_NET_H */
