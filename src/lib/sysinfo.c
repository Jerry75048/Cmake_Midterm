#include "sysinfo.h"

#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define WARN_FREE_RATIO 0.25
#define CRIT_FREE_RATIO 0.10

void sysinfo_collect(telemetry_t *out) {
    memset(out, 0, sizeof(*out));

    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(out->hostname, sizeof(out->hostname), "%s", uts.nodename);
        snprintf(out->os_name, sizeof(out->os_name), "%s %s", uts.sysname, uts.release);
    }

    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out->uptime_sec = (double)si.uptime;
        out->load_avg_1 = (double)si.loads[0] / 65536.0;
        out->mem_total_kb = (uint64_t)si.totalram * si.mem_unit / 1024;
        out->mem_free_kb  = (uint64_t)si.freeram  * si.mem_unit / 1024;
    }

    out->cpu_count = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);

    double free_ratio = out->mem_total_kb > 0
        ? (double)out->mem_free_kb / (double)out->mem_total_kb
        : 1.0;

    /* Rule-based "edge intelligence": classify locally so the client only needs
       the summary, not raw numbers it has to interpret itself. This is a simple
       threshold rule, not a learned model -- documented as such in the report. */
    if (free_ratio < CRIT_FREE_RATIO) {
        out->status = STATUS_CRITICAL;
    } else if (free_ratio < WARN_FREE_RATIO) {
        out->status = STATUS_WARNING;
    } else {
        out->status = STATUS_NORMAL;
    }
}

const char *sysinfo_status_str(int32_t status) {
    switch (status) {
        case STATUS_WARNING:  return "WARNING";
        case STATUS_CRITICAL: return "CRITICAL";
        default:                return "NORMAL";
    }
}
