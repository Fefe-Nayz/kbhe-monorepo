#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

// Actuation point in µm
#define DEFAULT_ACTUATION_POINT 1200 // 1.2mm

// Rapid trigger sensitivity in µm (distance from actuation point to trigger)
#define DEFAULT_RAPID_TRIGGER_SENSITIVITY 200 // 0.2mm

typedef struct {
    uint16_t primary_keycode;
    uint16_t actuation_point;
    uint16_t release_point;
    
    // Rapid trigger settings
    bool is_rapid_trigger_enabled;
    bool continuous_rapid_trigger;
    uint16_t rapid_trigger_press_sensitivity;
    uint16_t rapid_trigger_release_sensitivity;

    // Advanced per-key behaviors
    key_behavior_mode_t behavior_mode;
    uint16_t hold_threshold_ms;
    uint16_t secondary_keycode;
    uint8_t dynamic_zone_count;
    settings_dynamic_zone_t dynamic_zones[SETTINGS_DYNAMIC_ZONE_COUNT];
} key_trigger_settings_t;

typedef struct {
    int16_t last_distance;
    int16_t max_bottom_distance;
    int16_t min_top_distance;
    bool continuous_armed;
} key_rapid_trigger_data_t;

typedef struct {
    uint32_t press_start_ms;
    uint16_t active_keycode;
    uint16_t pending_release_keycode;
    uint8_t active_dynamic_zone;
    bool tap_hold_secondary_active;
    bool tap_hold_pending;
    bool toggle_latched;
    bool toggle_hold_active;
    bool toggle_pending;
} key_behavior_runtime_t;

typedef enum key_state_e {
    RELEASED = 0,
    PRESSED = 1,
} key_state_e;

typedef enum {
    KEY_DIRECTION_NONE = 0,
    KEY_DIRECTION_DOWN = 1,
    KEY_DIRECTION_UP = 2
} key_direction_e;


void trigger_init(void);

void trigger_task(void);

key_state_e trigger_get_key_state(uint8_t key);

uint16_t trigger_get_distance_01mm(uint8_t key);

void trigger_reload_settings(void);

void trigger_apply_key_settings(uint8_t key, const settings_key_t *settings);
