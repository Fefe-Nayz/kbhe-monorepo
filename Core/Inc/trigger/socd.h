#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LAST_INPUT_WINS = 0,
    MOST_PRESSED_INPUT_WINS = 1,
} socd_resolution_e;

typedef struct {
    bool is_socd_enabled;
    socd_resolution_e resolution_mode;
    uint8_t linked_key;
} socd_key_settings_t;

void socd_init(void);

void socd_task(void);

void socd_load_settings(void);

void socd_on_press(uint8_t key);

void socd_on_release(uint8_t key);