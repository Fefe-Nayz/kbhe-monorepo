#include "diagnostics.h"

#include "main.h"

static volatile uint32_t diagnostics_perf_expiry_ms = 0u;

static inline uint32_t diagnostics_now_ms(void) {
  return HAL_GetTick();
}

void diagnostics_init(void) {
  diagnostics_perf_expiry_ms = 0u;
}

void diagnostics_live_ping(void) {
  diagnostics_perf_expiry_ms =
      diagnostics_now_ms() + DIAGNOSTICS_SESSION_TIMEOUT_MS;
}

bool diagnostics_is_perf_active(void) {
  uint32_t expiry_ms = diagnostics_perf_expiry_ms;

  if (expiry_ms == 0u) {
    return false;
  }

  if ((int32_t)(diagnostics_now_ms() - expiry_ms) >= 0) {
    diagnostics_perf_expiry_ms = 0u;
    return false;
  }

  return true;
}
