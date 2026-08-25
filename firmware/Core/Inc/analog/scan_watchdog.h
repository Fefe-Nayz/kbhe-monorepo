#ifndef KBHE_ANALOG_SCAN_WATCHDOG_H_
#define KBHE_ANALOG_SCAN_WATCHDOG_H_

#include <stdbool.h>
#include <stdint.h>

/* One complete board acquisition normally takes about 53 us and the complete
 * sensor-to-trigger loop about 106 us. A silent timer/ADC stall must not leave
 * the last immutable sensor snapshot exposed for 100 ms: that is long enough
 * to swallow an ordinary tap and to leave sensor-driven RGB apparently stuck.
 * Two milliseconds is still over six times the worst 326 us scan observed by
 * the all-key diagnostics with settings traffic and RGB active. */
#define ADC_SCAN_WATCHDOG_TIMEOUT_MS 2u

static inline bool adc_scan_watchdog_should_recover(bool fault_pending,
                                                    bool scan_complete,
                                                    uint32_t now_ms,
                                                    uint32_t last_progress_ms) {
  return fault_pending ||
         (!scan_complete &&
          (uint32_t)(now_ms - last_progress_ms) >=
              ADC_SCAN_WATCHDOG_TIMEOUT_MS);
}

#endif /* KBHE_ANALOG_SCAN_WATCHDOG_H_ */
