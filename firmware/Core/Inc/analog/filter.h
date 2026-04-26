#include <stdbool.h>
#include <stdint.h>

#define FILTER_DEFAULT_ENABLED 1U
#define FILTER_DEFAULT_NOISE_BAND 8U
#define FILTER_DEFAULT_ALPHA_MIN_DENOM 1U
#define FILTER_DEFAULT_ALPHA_MAX_DENOM 8U
#define FILTER_BYPASS_THRESHOLD 60U

#define FILTER_Q_SHIFT 10U
#define FILTER_Q_ONE (1U << FILTER_Q_SHIFT)

void filter_init(void);

void filter_reset(void);

uint16_t filter_compute_next_filtered_value(uint8_t key, uint16_t raw_value);

bool filter_is_initialized(void);

void filter_set_initialized(bool initialized);

bool filter_get_enabled(void);

void filter_set_enabled(bool enabled);

void filter_get_params(uint8_t *noise_band, uint8_t *alpha_min_denom,
                       uint8_t *alpha_max_denom);

void filter_set_params(uint8_t noise_band, uint8_t alpha_min_denom,
                       uint8_t alpha_max_denom);
