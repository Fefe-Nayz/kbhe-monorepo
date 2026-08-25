#ifndef REALTIME_SCAN_POLICY_H_
#define REALTIME_SCAN_POLICY_H_

#include <stdbool.h>

/* One full-board scan every 125 us is the minimum 8 kHz service contract. */
#define KBHE_SCAN_DEADLINE_US 125u

typedef enum {
  KBHE_REALTIME_SERVICE_ADC = 0,
  KBHE_REALTIME_SERVICE_USB,
  KBHE_REALTIME_SERVICE_BEST_EFFORT,
} kbhe_realtime_service_t;

/* A DMA snapshot that was already published must be consumed and its next
 * acquisition armed before entering TinyUSB's event-draining loop. */
static inline kbhe_realtime_service_t
kbhe_realtime_first_service(bool adc_scan_complete) {
  return adc_scan_complete ? KBHE_REALTIME_SERVICE_ADC
                           : KBHE_REALTIME_SERVICE_USB;
}

/* TinyUSB vends no public event-count budget. Recheck the DMA publication
 * flag after its one service window and suppress all lower-priority work when
 * the next immutable scan is waiting. */
static inline kbhe_realtime_service_t
kbhe_realtime_after_usb(bool adc_scan_complete) {
  return adc_scan_complete ? KBHE_REALTIME_SERVICE_ADC
                           : KBHE_REALTIME_SERVICE_BEST_EFFORT;
}

#endif /* REALTIME_SCAN_POLICY_H_ */
