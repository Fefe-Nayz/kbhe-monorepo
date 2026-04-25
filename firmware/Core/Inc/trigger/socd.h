#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

typedef settings_socd_resolution_t socd_resolution_e;

typedef struct {
    bool is_socd_enabled;
    bool fully_pressed_enabled;
    socd_resolution_e resolution_mode;
    uint16_t fully_pressed_point_um;
    uint8_t linked_key;
} socd_key_settings_t;

void socd_init(void);

void socd_task(void);

void socd_load_settings(void);

void socd_on_press(uint8_t key);

void socd_on_release(uint8_t key);
