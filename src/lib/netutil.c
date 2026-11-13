#include "netutil.h"
#include "log.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

ssize_t recv_all(int fd, void *buf, size_t len) {
    char *p = buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break; /* peer closed */
        got += (size_t)n;
    }
    return (ssize_t)got;
}

int send_msg(int fd, uint16_t type, const void *payload, uint32_t len) {
    msg_header_t hdr;
    hdr.magic = PROTO_MAGIC;
    hdr.version = PROTO_VERSION;
    hdr.type = type;
    hdr.length = len;

    if (send_all(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) return -1;
    if (len > 0) {
        if (send_all(fd, payload, len) != (ssize_t)len) return -1;
    }
    return 0;
}

int recv_msg(int fd, msg_header_t *hdr_out, void *payload_buf, uint32_t buf_cap, uint32_t *out_len) {
    msg_header_t hdr;
    ssize_t r = recv_all(fd, &hdr, sizeof(hdr));
    if (r == 0) return 0;
    if (r != (ssize_t)sizeof(hdr)) return -1;

    if (hdr.magic != PROTO_MAGIC || hdr.version != PROTO_VERSION) {
        LOG_WARN("bad header (magic=0x%x version=%u) from fd=%d", hdr.magic, hdr.version, fd);
        return -2;
    }
    /* Robustness guard: never trust the on-wire length field blindly. Reject anything
       past the protocol-wide cap, or past what the caller's buffer can actually hold,
       before touching the socket again. */
    if (hdr.length > PROTO_MAX_PAYLOAD || hdr.length > buf_cap) {
        LOG_WARN("rejecting oversized payload length=%u (cap=%u) from fd=%d", hdr.length, buf_cap, fd);
        return -3;
    }
    if (hdr.length > 0) {
        r = recv_all(fd, payload_buf, hdr.length);
        if (r != (ssize_t)hdr.length) return -1;
    }

    *hdr_out = hdr;
    if (out_len) *out_len = hdr.length;
    return 1;
}

int net_listen(int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

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

int net_connect(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
