#include <stdbool.h>
#include <stdint.h>

// Actuation point in µm
#define DEFAULT_ACTUATION_POINT 1200 // 1.2mm

// Rapid trigger sensitivity in µm (distance from actuation point to trigger)
#define DEFAULT_RAPID_TRIGGER_SENSITIVITY 200 // 0.2mm

typedef struct {
    uint16_t actuation_point;
    
    // Rapid trigger settings
    bool is_rapid_trigger_enabled;
    bool use_rapid_trigger_press_sensitivity;
    uint16_t rapid_trigger_press_sensitivity;
    uint16_t rapid_trigger_release_sensitivity;
} key_trigger_settings_t;

typedef struct {
    int16_t last_distance;
    int16_t max_bottom_distance;
    int16_t min_top_distance;
} key_rapid_trigger_data_t;

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