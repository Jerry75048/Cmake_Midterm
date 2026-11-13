#ifndef LOG_H
#define LOG_H

/*
 * Two-level debug control:
 *  - compile-time: DEBUG_BUILD (set via `make debug`) decides whether LOG_DEBUG(...)
 *    calls exist in the binary at all. When not defined, LOG_DEBUG expands to nothing,
 *    so debug tracing has zero footprint in a release build.
 *  - runtime: log_set_level() (wired to -v on the CLI / LOG_LEVEL env var) decides,
 *    among whichever levels were compiled in, which ones actually print right now,
 *    with no recompile needed.
 */

typedef enum {
    LOGLEVEL_ERROR = 0,
    LOGLEVEL_WARN  = 1,
    LOGLEVEL_INFO  = 2,
    LOGLEVEL_DEBUG = 3
} log_level_t;

void log_set_level(log_level_t level);
log_level_t log_level_from_string(const char *s);
void log_write(log_level_t level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define LOG_ERROR(...) log_write(LOGLEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOGLEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOGLEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_BUILD
#define LOG_DEBUG(...) log_write(LOGLEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...) do {} while (0)
#endif

#endif /* LOG_H */
