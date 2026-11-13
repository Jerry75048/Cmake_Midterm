#include "protocol.h"
#include "netutil.h"
#include "log.h"
#include "sysinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define DEFAULT_PORT 9000

static void print_usage(const char *prog) {
    fprintf(stderr,
        "usage: %s -h <host> [-p port] [-v level] <command> [args]\n"
        "commands:\n"
        "  telemetry                 query the server's telemetry / status\n"
        "  get <remote> [local]      download a file from the server\n"
        "  put <local> [-F bytes]    upload a file to the server\n"
        "                              -F overrides the declared size (robustness demo only)\n"
        "  hold <seconds>            connect and idle N seconds (concurrency proof)\n",
        prog);
}

static void cmd_telemetry(int fd) {
    if (send_msg(fd, MSG_TELEMETRY_REQ, NULL, 0) != 0) {
        LOG_ERROR("failed to send telemetry request");
        return;
    }
    msg_header_t hdr;
    telemetry_t t;
    uint32_t len;
    int rc = recv_msg(fd, &hdr, &t, sizeof(t), &len);
    if (rc != 1 || hdr.type != MSG_TELEMETRY_RESP) {
        LOG_ERROR("telemetry request failed (rc=%d)", rc);
        return;
    }
    printf("host:      %s\n", t.hostname);
    printf("os:        %s\n", t.os_name);
    printf("uptime:    %.0f sec\n", t.uptime_sec);
    printf("load avg:  %.2f\n", t.load_avg_1);
    printf("cpu count: %d\n", t.cpu_count);
    printf("mem total: %llu KB\n", (unsigned long long)t.mem_total_kb);
    printf("mem free:  %llu KB\n", (unsigned long long)t.mem_free_kb);
    printf("status:    %s\n", sysinfo_status_str(t.status));
}

static int do_get(int fd, const char *remote, const char *local) {
    if (send_msg(fd, MSG_FILE_PULL_REQ, remote, (uint32_t)strlen(remote)) != 0) return -1;

    msg_header_t hdr;
    file_meta_t meta;
    uint32_t len;
    int rc = recv_msg(fd, &hdr, &meta, sizeof(meta), &len);
    if (rc != 1) { LOG_ERROR("get: no response from server"); return -1; }
    if (hdr.type == MSG_ERROR) {
        char msg[256] = {0};
        size_t n = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
        memcpy(msg, &meta, n);
        LOG_ERROR("get: server error: %s", msg);
        return -1;
    }
    if (hdr.type != MSG_FILE_META) { LOG_ERROR("get: unexpected response type %u", hdr.type); return -1; }

    FILE *fp = fopen(local, "wb");
    if (!fp) { LOG_ERROR("get: cannot open %s for writing", local); return -1; }

    uint64_t received = 0;
    char buf[FILE_CHUNK_SIZE];
    for (;;) {
        rc = recv_msg(fd, &hdr, buf, sizeof(buf), &len);
        if (rc <= 0) { LOG_ERROR("get: connection lost mid-transfer"); fclose(fp); return -1; }
        if (hdr.type == MSG_FILE_END) break;
        if (hdr.type != MSG_FILE_DATA) break;
        fwrite(buf, 1, len, fp);
        received += len;
    }
    fclose(fp);
    printf("downloaded %s -> %s (%llu bytes, server reported %llu)\n",
           remote, local, (unsigned long long)received, (unsigned long long)meta.filesize);
    return 0;
}

static int do_put(int fd, const char *local, const char *remote, int64_t fake_size) {
    FILE *fp = fopen(local, "rb");
    if (!fp) { LOG_ERROR("put: cannot open %s", local); return -1; }

    struct stat st;
    stat(local, &st);

    file_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    snprintf(meta.filename, sizeof(meta.filename), "%s", remote);
    meta.filesize = fake_size >= 0 ? (uint64_t)fake_size : (uint64_t)st.st_size;

    if (fake_size >= 0) {
        LOG_WARN("put: overriding declared size to %llu (actual file is %lld bytes) -- robustness demo mode",
                  (unsigned long long)fake_size, (long long)st.st_size);
    }

    send_msg(fd, MSG_FILE_PUSH_REQ, &meta, sizeof(meta));

    char buf[FILE_CHUNK_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        send_msg(fd, MSG_FILE_DATA, buf, (uint32_t)n);
    }
    fclose(fp);
    send_msg(fd, MSG_FILE_END, NULL, 0);

    msg_header_t hdr;
    char resp[256];
    uint32_t len;
    int rc = recv_msg(fd, &hdr, resp, sizeof(resp), &len);
    if (rc == 1 && hdr.type == MSG_ACK) {
        printf("upload acknowledged by server\n");
    } else if (rc == 1 && hdr.type == MSG_ERROR) {
        resp[len < sizeof(resp) ? len : sizeof(resp) - 1] = '\0';
        printf("server rejected upload: %s\n", resp);
    } else {
        printf("no clean response from server (rc=%d) -- connection may have been dropped\n", rc);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = DEFAULT_PORT;
    log_set_level(LOGLEVEL_INFO);

    int opt;
    while ((opt = getopt(argc, argv, "+h:p:v:")) != -1) {
        switch (opt) {
            case 'h': host = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 'v': log_set_level(log_level_from_string(optarg)); break;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) { print_usage(argv[0]); return 1; }
    const char *command = argv[optind];

    int fd = net_connect(host, port);
    if (fd < 0) {
        LOG_ERROR("cannot connect to %s:%d: %s", host, port, strerror(errno));
        return 1;
    }

    if (strcmp(command, "telemetry") == 0) {
        cmd_telemetry(fd);
    } else if (strcmp(command, "get") == 0) {
        if (optind + 1 >= argc) { print_usage(argv[0]); close(fd); return 1; }
        const char *remote = argv[optind + 1];
        const char *local = (optind + 2 < argc) ? argv[optind + 2] : remote;
        do_get(fd, remote, local);
    } else if (strcmp(command, "put") == 0) {
        if (optind + 1 >= argc) { print_usage(argv[0]); close(fd); return 1; }
        const char *local = argv[optind + 1];
        int64_t fake_size = -1;
        for (int i = optind + 2; i < argc; i++) {
            if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
                fake_size = atoll(argv[i + 1]);
                i++;
            }
        }
        const char *base = strrchr(local, '/');
        base = base ? base + 1 : local;
        do_put(fd, local, base, fake_size);
    } else if (strcmp(command, "hold") == 0) {
        int secs = (optind + 1 < argc) ? atoi(argv[optind + 1]) : 5;
        printf("pid=%d holding connection for %d seconds...\n", (int)getpid(), secs);
        sleep((unsigned)secs);
    } else {
        print_usage(argv[0]);
    }

    close(fd);
    return 0;
}
