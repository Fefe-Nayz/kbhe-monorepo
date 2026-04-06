#include <stdint.h>
#include <stdbool.h>

#define ALPHA_MIN 0.03
#define ALPHA_MAX 0.25
#define NOISE_BAND 30
#define BYPASS_THRESHOLD 60

#define ALPHA_COEFF (ALPHA_MAX - ALPHA_MIN) / (8 * NOISE_BAND - NOISE_BAND)

void filter_init(void);

uint16_t filter_compute_next_filtered_value(uint8_t key, uint16_t raw_value);

bool filter_is_initialized(void);

void filter_set_initialized(bool initialized);