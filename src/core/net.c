#include "net.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <netinet/tcp.h>

int net_resolve(const char *host, uint16_t port, struct sockaddr_in *out) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    int r = getaddrinfo(host, port_str, &hints, &res);
    if (r != 0 || !res) {
        log_msg("[net] resolve '%s': %s\n", host, gai_strerror(r));
        return 0;
    }
    *out = *(struct sockaddr_in*)res->ai_addr;
    freeaddrinfo(res);
    return 1;
}

int net_set_nonblock(socket_t fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

int net_set_tcp_receive_buffer(socket_t fd) {
    int receive_buffer_size = NET_TCP_RECEIVE_BUFFER_SIZE;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size,
                   sizeof(receive_buffer_size)) == 0) {
        return 1;
    }
    int primary_error = errno;
    if (primary_error == ENOBUFS) {
        receive_buffer_size = NET_TCP_RECEIVE_BUFFER_FALLBACK_SIZE;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size,
                       sizeof(receive_buffer_size)) == 0) {
            log_msg("[net] set TCP receive buffer after handshake: %s; "
                    "using %d-byte fallback\n",
                    strerror(primary_error), receive_buffer_size);
            return 1;
        }
        int fallback_error = errno;
        log_msg("[net] set TCP receive buffer after handshake: %s; "
                "%d-byte fallback failed: %s\n",
                strerror(primary_error), receive_buffer_size,
                strerror(fallback_error));
        return 0;
    }
    log_msg("[net] set TCP receive buffer after handshake: %s\n",
            strerror(primary_error));
    return 0;
}

/* Crash diagnostics. The count covers the sockets this module creates and closes,
   the DHT and uTP ones included (they come from net_udp_socket); only libcurl opens
   sockets outside it. Phase names are stored as pointers, so only string literals
   may be passed. */
static int gLiveSockets = 0;
static int gAcceptFailures = 0;
static int gSocketFailures = 0;
static const char *gPhase = "before main";     /* last phase any thread marked */

static void net_fd_opened(void) {
    __atomic_add_fetch(&gLiveSockets, 1, __ATOMIC_RELAXED);
    /* A socket that opened again ends the failure episode, so the next one is logged. */
    __atomic_store_n(&gSocketFailures, 0, __ATOMIC_RELAXED);
}

static void net_fd_closed(socket_t fd) {
    close(fd);
    __atomic_sub_fetch(&gLiveSockets, 1, __ATOMIC_RELAXED);
}

int net_live_socket_count(void) {
    return __atomic_load_n(&gLiveSockets, __ATOMIC_RELAXED);
}

void gh_diag_set_phase(const char *phase) {
    __atomic_store_n(&gPhase, phase ? phase : "?", __ATOMIC_RELAXED);
}

const char *gh_diag_phase(void) {
    return __atomic_load_n(&gPhase, __ATOMIC_RELAXED);
}

/* errno is saved and restored: tcpConnect reads it after a failed net_tcp_connect,
   and the log call must not change what it classifies. */
static void net_log_socket_failure(const char *what) {
    int err = errno;
    int n = __atomic_add_fetch(&gSocketFailures, 1, __ATOMIC_RELAXED);
    if (n <= 20 || n % 100 == 0)
        log_msg("[net] %s failed errno=%d live=%d (failure %d)\n", what, err,
                net_live_socket_count(), n);
    errno = err;
}

socket_t net_tcp_connect(const struct sockaddr_in *addr) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        net_log_socket_failure("socket()");
        return INVALID_SOCK;
    }
    net_fd_opened();
    net_set_nonblock(fd);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int r = connect(fd, (struct sockaddr*)addr, sizeof(*addr));
    if (r < 0 && errno != EINPROGRESS) {
        net_fd_closed(fd);
        return INVALID_SOCK;
    }
    return fd;
}

socket_t net_udp_socket(uint16_t local_port) {
    socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        net_log_socket_failure("socket() UDP");
        return INVALID_SOCK;
    }
    net_fd_opened();
    net_set_nonblock(fd);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (local_port > 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(local_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            log_msg("[net] bind UDP port %u: %s\n", local_port, strerror(errno));
            net_fd_closed(fd);
            return INVALID_SOCK;
        }
    }
    return fd;
}

socket_t net_tcp_listen(uint16_t port, int backlog) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        net_log_socket_failure("socket() TCP listener");
        return INVALID_SOCK;
    }
    net_fd_opened();
    net_set_nonblock(fd);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_msg("[net] bind TCP port %u: %s\n", port, strerror(errno));
        net_fd_closed(fd);
        return INVALID_SOCK;
    }
    if (listen(fd, backlog) < 0) {
        log_msg("[net] listen port %u: %s\n", port, strerror(errno));
        net_fd_closed(fd);
        return INVALID_SOCK;
    }
    return fd;
}

socket_t net_accept(socket_t listener, struct sockaddr_in *peer) {
    struct sockaddr_in tmp;
    socklen_t len = sizeof(tmp);
    socket_t fd;
    do {
        fd = accept(listener, (struct sockaddr*)&tmp, &len);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        /* A non-blocking listener reports EAGAIN when idle: normal, not logged.
           Anything else (EMFILE, ENOBUFS...) is the resource signal we want to see,
           and failures are rate-limited so a storm cannot flood the log. */
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            int n = __atomic_add_fetch(&gAcceptFailures, 1, __ATOMIC_RELAXED);
            if (n <= 20 || n % 100 == 0)
                log_msg("[net] accept() failed errno=%d live=%d (failure %d)\n",
                        err, net_live_socket_count(), n);
        }
        errno = err;
        return INVALID_SOCK;
    }
    net_fd_opened();
    /* A successful accept ends the failure episode of this listener. */
    __atomic_store_n(&gAcceptFailures, 0, __ATOMIC_RELAXED);
    net_set_nonblock(fd);
    /* Accepted sockets serve the web companion's small HTTP/SSE frames;
       Nagle only adds latency there. Best-effort. */
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    if (peer) *peer = tmp;
    return fd;
}

uint16_t net_local_port(socket_t fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr*)&addr, &len) < 0) return 0;
    return ntohs(addr.sin_port);
}

uint32_t net_local_ipv4(socket_t fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr*)&addr, &len) < 0) return 0;
    return ntohl(addr.sin_addr.s_addr);
}

void net_close(socket_t fd) {
    if (fd >= 0) net_fd_closed(fd);
}

ssize_t net_send(socket_t fd, const uint8_t *buf, size_t len) {
    /* Cap each send() call: libnx marshals the whole buffer through the bsd
       service transfer memory, and multi-megabyte writes (web catalog
       responses) can fail outright where a chunked series succeeds. Peer
       traffic never exceeds block size, so this only matters for the web
       server's large bodies. */
    const size_t max_chunk = 32 * 1024;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > max_chunk)
            chunk = max_chunk;
        ssize_t n = send(fd, buf + sent, chunk,
#ifdef MSG_NOSIGNAL
                         MSG_NOSIGNAL
#else
                         0
#endif
                         );
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return -1;
        }
        if (n == 0) break;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

int net_recv(socket_t fd, uint8_t *buf, size_t len) {
    ssize_t n;
    do {
        n = recv(fd, buf, len, 0);
    } while (n < 0 && errno == EINTR);
    return (int)n;
}
