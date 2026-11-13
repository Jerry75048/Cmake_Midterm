#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static log_level_t g_level = LOGLEVEL_INFO;

void log_set_level(log_level_t level) {
    g_level = level;
}

log_level_t log_level_from_string(const char *s) {
    if (!s) return LOGLEVEL_INFO;
    if (strcasecmp(s, "error") == 0) return LOGLEVEL_ERROR;
    if (strcasecmp(s, "warn")  == 0) return LOGLEVEL_WARN;
    if (strcasecmp(s, "info")  == 0) return LOGLEVEL_INFO;
    if (strcasecmp(s, "debug") == 0) return LOGLEVEL_DEBUG;
    return LOGLEVEL_INFO;
}

static const char *level_name(log_level_t level) {
    switch (level) {
        case LOGLEVEL_ERROR: return "ERROR";
        case LOGLEVEL_WARN:  return "WARN";
        case LOGLEVEL_INFO:  return "INFO";
        case LOGLEVEL_DEBUG: return "DEBUG";
        default:              return "?";
    }
}

void log_write(log_level_t level, const char *file, int line, const char *fmt, ...) {
    if (level > g_level) return;

    char timebuf[32];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_now);

    fprintf(stderr, "[%s] [%-5s] [pid %d] [%s:%d] ",
            timebuf, level_name(level), (int)getpid(), file, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
}
