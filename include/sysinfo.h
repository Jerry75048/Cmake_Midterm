#ifndef SYSINFO_H
#define SYSINFO_H

#include "protocol.h"

void sysinfo_collect(telemetry_t *out);
const char *sysinfo_status_str(int32_t status);

#endif /* SYSINFO_H */
