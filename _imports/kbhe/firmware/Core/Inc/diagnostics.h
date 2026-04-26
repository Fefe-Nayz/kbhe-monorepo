#ifndef DIAGNOSTICS_H_
#define DIAGNOSTICS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIAGNOSTICS_SESSION_TIMEOUT_MS 750u

void diagnostics_init(void);

void diagnostics_live_ping(void);

bool diagnostics_is_perf_active(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAGNOSTICS_H_ */
