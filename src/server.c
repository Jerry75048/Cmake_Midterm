#include "protocol.h"
#include "netutil.h"
#include "log.h"
#include "sysinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT     9000
#define BACKLOG          64
#define MAX_UPLOAD_SIZE  (64u * 1024 * 1024)  /* 64MB cap for pushed files */
#define SHARED_DIR       "shared"
#define UPLOAD_DIR       "uploads"

static volatile sig_atomic_t g_active_children = 0;

#ifndef DEMO_NO_SIGCHLD
/* Robustness #1: reap every child as soon as it exits so none linger as zombies.
   Only async-signal-safe calls are used here (no logging, no malloc). */
static void on_sigchld(int signo) {
    (void)signo;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        g_active_children--;
    }
    errno = saved_errno;
}
#endif

static int sanitize_filename(const char *in, char *out, size_t outsz) {
    if (!in || in[0] == '\0') return -1;
    if (strstr(in, "..") != NULL) return -1;
    if (in[0] == '/' || in[0] == '\\') return -1;
    size_t n = strnlen(in, outsz);
    if (n == 0 || n >= outsz) return -1;
    memcpy(out, in, n);
    out[n] = '\0';
    return 0;
}

static void handle_telemetry(int fd) {
    telemetry_t t;
    sysinfo_collect(&t);
    LOG_INFO("telemetry -> fd=%d status=%s mem_free=%lluKB",
             fd, sysinfo_status_str(t.status), (unsigned long long)t.mem_free_kb);
    send_msg(fd, MSG_TELEMETRY_RESP, &t, sizeof(t));
}

static void handle_file_pull(int fd, const char *raw_name) {
    char name[MAX_FILENAME];
    if (sanitize_filename(raw_name, name, sizeof(name)) != 0) {
        const char *err = "invalid filename";
        send_msg(fd, MSG_ERROR, err, (uint32_t)strlen(err));
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, name);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        const char *err = "file not found";
        LOG_WARN("pull: %s not found (fd=%d)", path, fd);
        send_msg(fd, MSG_ERROR, err, (uint32_t)strlen(err));
        return;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        fclose(fp);
        const char *err = "stat failed";
        send_msg(fd, MSG_ERROR, err, (uint32_t)strlen(err));
        return;
    }

    file_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    snprintf(meta.filename, sizeof(meta.filename), "%s", name);
    meta.filesize = (uint64_t)st.st_size;
    send_msg(fd, MSG_FILE_META, &meta, sizeof(meta));

    char buf[FILE_CHUNK_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        send_msg(fd, MSG_FILE_DATA, buf, (uint32_t)n);
    }
    fclose(fp);
    send_msg(fd, MSG_FILE_END, NULL, 0);
    LOG_INFO("pull: sent %s (%lld bytes) to fd=%d", path, (long long)st.st_size, fd);
}

static void handle_file_push(int fd, const file_meta_t *meta) {
    char name[MAX_FILENAME];
    if (sanitize_filename(meta->filename, name, sizeof(name)) != 0) {
        const char *err = "invalid filename";
        send_msg(fd, MSG_ERROR, err, (uint32_t)strlen(err));
        return;
    }

#ifndef DEMO_NO_LENCHECK
    /* Robustness #3: never trust a client-declared size. Reject anything over the
       sane cap up front, and stream to disk in fixed-size chunks so a single
       request can never force a giant allocation. */
    if (meta->filesize > MAX_UPLOAD_SIZE) {
        LOG_WARN("push: rejecting %s, declared size %llu exceeds cap",
                  name, (unsigned long long)meta->filesize);
        const char *err = "file too large";
        send_msg(fd, MSG_ERROR, err, (uint32_t)strlen(err));
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", UPLOAD_DIR, name);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        const char *err = "cannot open destination";
        send_msg(fd, MSG_ERROR, err, (uint32_t)strlen(err));
        return;
    }

    uint64_t received = 0;
    char buf[FILE_CHUNK_SIZE];
    for (;;) {
        msg_header_t hdr;
        uint32_t got;
        int rc = recv_msg(fd, &hdr, buf, sizeof(buf), &got);
        if (rc <= 0) { LOG_WARN("push: connection lost from fd=%d", fd); fclose(fp); return; }
        if (hdr.type == MSG_FILE_END) break;
        if (hdr.type != MSG_FILE_DATA) { LOG_WARN("push: unexpected msg type %u", hdr.type); break; }
        fwrite(buf, 1, got, fp);
        received += got;
    }
    fclose(fp);

    if (received != meta->filesize) {
        LOG_WARN("push: size mismatch for %s: declared=%llu received=%llu",
                  name, (unsigned long long)meta->filesize, (unsigned long long)received);
    }
    LOG_INFO("push: saved %s (%llu bytes) from fd=%d", path, (unsigned long long)received, fd);
    send_msg(fd, MSG_ACK, NULL, 0);

#else
    /* INTENTIONALLY VULNERABLE DEMO PATH (built only via `make bin/server_no_lencheck`):
       a small fixed stack buffer, but instead of passing its real size as the cap
       to recv_msg, passes the client-declared remaining size. recv_msg's own
       oversized-payload guard compares the incoming chunk length against *that*
       cap, not against local_buf's true 64 bytes, so it never trips -- any chunk
       bigger than 64 bytes (the default FILE_CHUNK_SIZE is 4096) overflows the
       stack. An ordinary upload of any file larger than 64 bytes triggers this;
       no declared-size override is needed. */
    (void)name;
    LOG_WARN("push: DEMO_NO_LENCHECK active, capping recv_msg with the client-declared "
              "size instead of local_buf's real size (fd=%d)", fd);
    char local_buf[64];
    uint64_t off = 0;
    while (off < meta->filesize) {
        msg_header_t hdr;
        uint32_t got;
        int rc = recv_msg(fd, &hdr, local_buf + off, (uint32_t)(meta->filesize - off), &got);
        if (rc <= 0 || hdr.type != MSG_FILE_DATA) break;
        off += got;
    }
#endif
}

static void handle_client(int fd) {
    for (;;) {
        msg_header_t hdr;
        char payload[FILE_CHUNK_SIZE];
        uint32_t len;
        int rc = recv_msg(fd, &hdr, payload, sizeof(payload), &len);

        if (rc == 0) { LOG_INFO("fd=%d disconnected", fd); break; }
        if (rc == -1) { LOG_WARN("fd=%d io error: %s", fd, strerror(errno)); break; }
        if (rc == -2) { send_msg(fd, MSG_ERROR, "bad header", 10); break; }
        if (rc == -3) { send_msg(fd, MSG_ERROR, "payload too large", 18); break; }

        switch (hdr.type) {
            case MSG_TELEMETRY_REQ:
                handle_telemetry(fd);
                break;
            case MSG_FILE_PULL_REQ:
                payload[len < sizeof(payload) ? len : sizeof(payload) - 1] = '\0';
                handle_file_pull(fd, payload);
                break;
            case MSG_FILE_PUSH_REQ:
                if (len == sizeof(file_meta_t)) {
                    file_meta_t meta;
                    memcpy(&meta, payload, sizeof(meta));
                    handle_file_push(fd, &meta);
                } else {
                    LOG_WARN("fd=%d malformed push request (len=%u)", fd, len);
                }
                break;
            default:
                LOG_WARN("fd=%d unknown message type %u", fd, hdr.type);
        }
    }
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    log_set_level(LOGLEVEL_INFO);

    int opt;
    while ((opt = getopt(argc, argv, "p:v:")) != -1) {
        switch (opt) {
            case 'p': port = atoi(optarg); break;
            case 'v': log_set_level(log_level_from_string(optarg)); break;
            default:
                fprintf(stderr, "usage: %s [-p port] [-v error|warn|info|debug]\n", argv[0]);
                return 1;
        }
    }

    mkdir(SHARED_DIR, 0755);
    mkdir(UPLOAD_DIR, 0755);

#ifndef DEMO_NO_SIGPIPE
    /* Robustness #2: a write() to a socket the peer already closed raises SIGPIPE,
       whose default action terminates the process. Ignore it so send_all() just
       sees EPIPE and we can fail that one connection instead of dying. */
    signal(SIGPIPE, SIG_IGN);
#else
    LOG_WARN("DEMO_NO_SIGPIPE active: SIGPIPE not ignored, a write to a closed socket can kill this process");
#endif

#ifndef DEMO_NO_SIGCHLD
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigchld;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
#else
    LOG_WARN("DEMO_NO_SIGCHLD active: finished children will remain as zombies");
#endif

    int listen_fd = net_listen(port, BACKLOG);
    if (listen_fd < 0) {
        LOG_ERROR("failed to listen on port %d: %s", port, strerror(errno));
        return 1;
    }
    LOG_INFO("server listening on port %d (pid=%d)", port, (int)getpid());

    for (;;) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int conn_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (conn_fd < 0) {
            if (errno == EINTR) continue;
            LOG_WARN("accept failed: %s", strerror(errno));
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));

        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork failed: %s", strerror(errno));
            close(conn_fd);
            continue;
        }
        if (pid == 0) {
            close(listen_fd);
            LOG_INFO("child pid=%d serving %s:%d (fd=%d)",
                     (int)getpid(), ip, ntohs(cli_addr.sin_port), conn_fd);
            handle_client(conn_fd);
            close(conn_fd);
            LOG_INFO("child pid=%d exiting", (int)getpid());
            exit(0);
        }

        g_active_children++;
        close(conn_fd);
        LOG_DEBUG("parent: forked child pid=%d, active_children=%d", (int)pid, g_active_children);
    }

    return 0;
}
