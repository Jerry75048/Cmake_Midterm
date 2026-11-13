#ifndef NETUTIL_H
#define NETUTIL_H

#include <sys/types.h>
#include "protocol.h"

ssize_t send_all(int fd, const void *buf, size_t len);
ssize_t recv_all(int fd, void *buf, size_t len);

int send_msg(int fd, uint16_t type, const void *payload, uint32_t len);

/*
 * Returns: 1 ok, 0 peer closed cleanly, -1 I/O error,
 *          -2 bad magic/version, -3 declared length rejected (robustness guard).
 */
int recv_msg(int fd, msg_header_t *hdr_out, void *payload_buf, uint32_t buf_cap, uint32_t *out_len);

int net_listen(int port, int backlog);
int net_connect(const char *host, int port);

#endif /* NETUTIL_H */
