#include <stdint.h>
#include <stdbool.h>

#define NOISE_BAND 30
#define BYPASS_THRESHOLD 60

#define FILTER_Q_SHIFT 10U
#define FILTER_Q_ONE (1U << FILTER_Q_SHIFT)

#define ALPHA_MIN_Q 31U
#define ALPHA_MAX_Q 256U
#define ALPHA_RAMP_DENOM (8U * NOISE_BAND - NOISE_BAND)

void filter_init(void);

uint16_t filter_compute_next_filtered_value(uint8_t key, uint16_t raw_value);

bool filter_is_initialized(void);

void filter_set_initialized(bool initialized);