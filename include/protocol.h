#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTO_MAGIC        0x4E535053u   /* "NSPS" */
#define PROTO_VERSION      1
#define PROTO_MAX_PAYLOAD  (1u * 1024 * 1024)  /* hard cap enforced by netutil regardless of any demo flag */

typedef enum {
    MSG_TELEMETRY_REQ  = 1,
    MSG_TELEMETRY_RESP = 2,
    MSG_FILE_PULL_REQ  = 3,  /* payload: filename string (client -> server) */
    MSG_FILE_PUSH_REQ  = 4,  /* payload: file_meta_t    (client -> server) */
    MSG_FILE_META      = 5,  /* payload: file_meta_t    (server -> client, reply to PULL) */
    MSG_FILE_DATA      = 6,  /* payload: up to FILE_CHUNK_SIZE bytes of file content */
    MSG_FILE_END       = 7,  /* payload: none, marks end of a file transfer */
    MSG_ACK            = 8,  /* payload: none */
    MSG_ERROR          = 9   /* payload: short human-readable error string */
} msg_type_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;   /* payload length in bytes, immediately following this header on the wire */
} msg_header_t;

typedef enum {
    STATUS_NORMAL   = 0,
    STATUS_WARNING  = 1,
    STATUS_CRITICAL = 2
} node_status_t;

/* Telemetry = the edge node's health report (the required "system info" feature). */
typedef struct {
    char   hostname[128];
    char   os_name[150];
    double uptime_sec;
    double load_avg_1;
    uint64_t mem_total_kb;
    uint64_t mem_free_kb;
    int32_t cpu_count;
    int32_t status;     /* node_status_t: rule-based classification, computed server-side */
} telemetry_t;

#define MAX_FILENAME 256

typedef struct {
    char     filename[MAX_FILENAME];
    uint64_t filesize;
} file_meta_t;

#define FILE_CHUNK_SIZE 4096

#endif /* PROTOCOL_H */
