#include <stdint.h>

#define ALPHA_MIN 0.03
#define ALPHA_MAX 0.25
#define NOISE_BAND 30
#define BYPASS_THRESHOLD 50

#define ALPHA_COEFF (ALPHA_MAX - ALPHA_MIN) / (BYPASS_THRESHOLD - NOISE_BAND)

uint16_t filter_compute_next_filtered_value(uint16_t new_raw_value, uint16_t previous_filtered_value);