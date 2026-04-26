/*
 * hid_protocol.h
 * RAW HID Command Protocol for keyboard configuration
 */

#ifndef HID_PROTOCOL_H_
#define HID_PROTOCOL_H_

#include "settings.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Protocol Constants
//--------------------------------------------------------------------+
#define HID_PROTOCOL_PACKET_SIZE 64

// Response status codes
#define HID_RESP_OK 0x00
#define HID_RESP_ERROR 0x01
#define HID_RESP_INVALID_CMD 0x02
#define HID_RESP_INVALID_PARAM 0x03

//--------------------------------------------------------------------+
// Command IDs
//--------------------------------------------------------------------+
typedef enum {
  // System commands (0x00 - 0x1F)
  CMD_GET_FIRMWARE_VERSION = 0x00,
  CMD_REBOOT = 0x01,
  CMD_ENTER_BOOTLOADER = 0x02,
  CMD_FACTORY_RESET = 0x03,
  CMD_USB_REENUMERATE = 0x04,

  // Settings commands (0x20 - 0x3F)
  CMD_GET_OPTIONS = 0x20,
  CMD_SET_OPTIONS = 0x21,
  CMD_GET_KEYBOARD_ENABLED = 0x22,
  CMD_SET_KEYBOARD_ENABLED = 0x23,
  CMD_GET_GAMEPAD_ENABLED = 0x24,
  CMD_SET_GAMEPAD_ENABLED = 0x25,
  CMD_SAVE_SETTINGS = 0x26,
  CMD_GET_NKRO_ENABLED = 0x27,
  CMD_SET_NKRO_ENABLED = 0x28,
  CMD_GET_ADVANCED_TICK_RATE = 0x29,
  CMD_SET_ADVANCED_TICK_RATE = 0x2A,
  CMD_GET_DEVICE_INFO = 0x2B,
  CMD_GET_KEYBOARD_NAME = 0x2C,
  CMD_SET_KEYBOARD_NAME = 0x2D,
  CMD_COPY_PROFILE_SLOT = 0x2E,
  CMD_RESET_PROFILE_SLOT = 0x2F,
  CMD_GET_DEFAULT_PROFILE = 0x30,       // Get default boot profile index
  CMD_SET_DEFAULT_PROFILE = 0x31,       // Set default boot profile index
  CMD_GET_RAM_ONLY_MODE = 0x32,         // Get RAM-only mode state
  CMD_SET_RAM_ONLY_MODE = 0x33,         // Enter/exit RAM-only mode (suppress flash saves)
  CMD_RELOAD_SETTINGS_FROM_FLASH = 0x34, // Exit RAM-only and reload last-saved settings
  CMD_GET_TRIGGER_CHATTER_GUARD = 0x35, // Get anti-chatter guard runtime setting
  CMD_SET_TRIGGER_CHATTER_GUARD = 0x36, // Set anti-chatter guard runtime setting

  // Key settings commands (0x40 - 0x5F)
  CMD_GET_KEY_SETTINGS = 0x40,
  CMD_SET_KEY_SETTINGS = 0x41,
  CMD_GET_ALL_KEY_SETTINGS = 0x42,
  CMD_SET_ALL_KEY_SETTINGS = 0x43,
  CMD_GET_GAMEPAD_SETTINGS = 0x44,
  CMD_SET_GAMEPAD_SETTINGS = 0x45,
  CMD_GET_CALIBRATION = 0x46,
  CMD_SET_CALIBRATION = 0x47,
  CMD_AUTO_CALIBRATE = 0x48, // Auto-calibrate a key (read current ADC as zero)
  CMD_GET_KEY_CURVE = 0x49,  // Get per-key analog curve
  CMD_SET_KEY_CURVE = 0x4A,  // Set per-key analog curve
  CMD_GET_KEY_GAMEPAD_MAP = 0x4B, // Get per-key gamepad mapping
  CMD_SET_KEY_GAMEPAD_MAP = 0x4C, // Set per-key gamepad mapping
  // 0x4D, 0x4E: reserved (previously legacy gamepad-with-kb commands, removed)
  CMD_GET_CALIBRATION_MAX = 0x4F, // Get per-key max ADC calibration values
  CMD_SET_CALIBRATION_MAX = 0x50, // Set per-key max ADC calibration values
  CMD_GUIDED_CALIBRATION_START = 0x51,
  CMD_GUIDED_CALIBRATION_STATUS = 0x52,
  CMD_GUIDED_CALIBRATION_ABORT = 0x53,
  CMD_GET_ROTARY_ENCODER_SETTINGS = 0x54,
  CMD_SET_ROTARY_ENCODER_SETTINGS = 0x55,
  CMD_GET_LAYER_KEYCODE = 0x56,
  CMD_SET_LAYER_KEYCODE = 0x57,
  CMD_RESET_KEY_TRIGGER_SETTINGS = 0x58,
  CMD_GET_ROTARY_STATE = 0x59,
  CMD_GET_ACTIVE_PROFILE = 0x5A,
  CMD_SET_ACTIVE_PROFILE = 0x5B,
  CMD_GET_PROFILE_NAME = 0x5C,
  CMD_SET_PROFILE_NAME = 0x5D,
  CMD_CREATE_PROFILE = 0x5E,
  CMD_DELETE_PROFILE = 0x5F,

  // LED Matrix commands (0x60 - 0x7F)
  CMD_GET_LED_ENABLED = 0x60,
  CMD_SET_LED_ENABLED = 0x61,
  CMD_GET_LED_BRIGHTNESS = 0x62,
  CMD_SET_LED_BRIGHTNESS = 0x63,
  CMD_GET_LED_PIXEL = 0x64,
  CMD_SET_LED_PIXEL = 0x65,
  CMD_GET_LED_ROW = 0x66,
  CMD_SET_LED_ROW = 0x67,
  CMD_GET_LED_ALL = 0x68,       // Returns first chunk (needs multiple calls)
  CMD_SET_LED_ALL = 0x69,       // Sets all LEDs (needs multiple calls)
  CMD_SET_LED_ALL_CHUNK = 0x6A, // Set LED data in chunks
  CMD_LED_CLEAR = 0x6B,
  CMD_LED_FILL = 0x6C,
  CMD_LED_TEST_RAINBOW = 0x6D,
  CMD_GET_LED_EFFECT = 0x6E,       // Get current LED effect mode
  CMD_SET_LED_EFFECT = 0x6F,       // Set LED effect mode
  CMD_GET_LED_FPS_LIMIT = 0x70,    // Get LED FPS limit
  CMD_SET_LED_FPS_LIMIT = 0x71,    // Set LED FPS limit
  CMD_GET_LED_EFFECT_PARAMS = 0x72, // Get persisted params for one effect
  CMD_SET_LED_EFFECT_PARAMS = 0x73, // Set persisted params for one effect
  CMD_SET_LED_VOLUME_OVERLAY = 0x74, // Host-driven volume overlay
  CMD_CLEAR_LED_VOLUME_OVERLAY = 0x75, // Clear host-driven volume overlay
  CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY = 0x76, // Restore effect active before third-party live override
  CMD_GET_LED_EFFECT_SCHEMA = 0x77, // Read param schema chunk for one effect
  CMD_SET_LED_AUDIO_SPECTRUM = 0x78, // Push host audio spectrum payload
  CMD_CLEAR_LED_AUDIO_SPECTRUM = 0x79, // Clear host audio spectrum state
  CMD_SET_LED_ALPHA_MASK = 0x7A, // Set/clear alpha mask (len + bitmask bytes)
  CMD_GET_LED_IDLE_OPTIONS = 0x7B, // Get LED idle timeout and idle policies
  CMD_SET_LED_IDLE_OPTIONS = 0x7C, // Set LED idle timeout and idle policies
  CMD_GET_LED_USB_SUSPEND_RGB_OFF = 0x7D, // Get USB suspend RGB-off policy
  CMD_SET_LED_USB_SUSPEND_RGB_OFF = 0x7E, // Set USB suspend RGB-off policy

  // ADC Filter commands (0x80 - 0x8F)
  CMD_GET_FILTER_ENABLED = 0x80, // Get filter enabled state
  CMD_SET_FILTER_ENABLED = 0x81, // Set filter enabled state
  CMD_GET_FILTER_PARAMS = 0x82,  // Get filter parameters
  CMD_SET_FILTER_PARAMS = 0x83,  // Set filter parameters

  // Debug commands (0xE0 - 0xEF)
  CMD_GET_ADC_VALUES = 0xE0,
  CMD_GET_KEY_STATES = 0xE1,
  CMD_GET_LOCK_STATES = 0xE2, // Get Caps/Num/Scroll lock states
  CMD_ADC_CAPTURE_START = 0xE3,
  CMD_ADC_CAPTURE_STATUS = 0xE4,
  CMD_ADC_CAPTURE_READ = 0xE5,
  CMD_GET_RAW_ADC_CHUNK = 0xE6,
  CMD_GET_FILTERED_ADC_CHUNK = 0xE7,
  CMD_GET_CALIBRATED_ADC_CHUNK = 0xE8,
  CMD_GET_MCU_METRICS = 0xE9,

  // Echo command for testing (0xFE)
  CMD_ECHO = 0xFE,

  // Unknown command
  CMD_UNKNOWN = 0xFF,
} hid_command_id_t;

//--------------------------------------------------------------------+
// Command Packet Structure
// Byte 0: Command ID
// Byte 1: Status (response) or Parameter length (request)
// Bytes 2-63: Payload
//--------------------------------------------------------------------+

#define HID_RAW_ADC_VALUES_PER_CHUNK 30
#define HID_KEY_SETTINGS_PER_CHUNK 6
#define HID_CALIBRATION_VALUES_PER_CHUNK 29
#define HID_KEY_STATES_PER_CHUNK 15
#define HID_LED_BYTES_PER_CHUNK 60
#define HID_DEVICE_SERIAL_MAX_LEN 26

/**
 * @brief Generic command packet header
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status_or_len;
  uint8_t payload[62];
} hid_packet_t;

/**
 * @brief Firmware version response
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint16_t version;
  uint8_t reserved[60];
} hid_resp_firmware_version_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint16_t version;
  char serial[HID_DEVICE_SERIAL_MAX_LEN];
  char keyboard_name[SETTINGS_KEYBOARD_NAME_LENGTH];
  uint8_t reserved[2];
} hid_packet_device_info_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  char keyboard_name[SETTINGS_KEYBOARD_NAME_LENGTH];
  uint8_t reserved[30];
} hid_packet_keyboard_name_t;

/**
 * @brief Options get/set packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t keyboard_enabled;
  uint8_t gamepad_enabled;
  uint8_t raw_hid_echo;
  uint8_t led_thermal_protection_enabled;
  uint8_t reserved[58];
} hid_packet_options_t;

/**
 * @brief Boolean setting packet (for get/set single bool)
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t value;
  uint8_t reserved[61];
} hid_packet_bool_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t key_index;
  uint8_t reserved[61];
} hid_packet_key_index_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t tick_rate;
  uint8_t reserved[61];
} hid_packet_tick_rate_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t enabled;
  uint8_t duration_ms;
  uint8_t reserved[60];
} hid_packet_trigger_chatter_guard_t;

/**
 * @brief Key settings packet (for single key)
 * Updated for enhanced rapid trigger settings
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t key_index;
  uint8_t profile_index;
  uint8_t layer_index;
  uint16_t hid_keycode; // HID/custom keycode for this key
  uint8_t actuation_point_mm;   // Actuation point in 0.1mm
  uint8_t release_point_mm;     // Release point in 0.1mm
  uint8_t rapid_trigger_press;      // RT press sensitivity in 0.01mm
  uint8_t rapid_trigger_release;    // RT release sensitivity in 0.01mm
  uint8_t socd_pair;                // SOCD paired key index (255 = none)
  uint8_t socd_resolution;          // settings_socd_resolution_t
  uint8_t rapid_trigger_enabled; // RT enable flag
  uint8_t disable_kb_on_gamepad; // Disable keyboard when gamepad active
  uint8_t continuous_rapid_trigger; // Continue RT until full release
  uint8_t behavior_mode;            // key_behavior_mode_t
  uint8_t hold_threshold_10ms;      // Tap-hold / toggle threshold
  uint16_t secondary_hid_keycode;   // Hold / alternate action
  struct __attribute__((packed)) {
    uint8_t end_mm_tenths; // DKS action bitmap
    uint16_t hid_keycode;  // DKS binding keycode
  } dynamic_zones[SETTINGS_DYNAMIC_ZONE_COUNT];
  uint8_t tap_hold_options;         // bit0=hold on other key press, bit1=uppercase hold
  uint8_t dks_bottom_out_point;     // DKS bottom-out point in 0.1mm
  uint8_t socd_fully_pressed_enabled;
  uint8_t socd_fully_pressed_point; // 0.1mm
  uint8_t reserved[28];
} hid_packet_key_settings_t;

/**
 * @brief Compact per-key settings entry for bulk HID transfers.
 */
typedef struct __attribute__((packed)) {
  uint16_t hid_keycode;
  uint8_t actuation_point_mm;
  uint8_t release_point_mm;
  uint8_t rapid_trigger_press;
  uint8_t rapid_trigger_release;
  uint8_t socd_pair;
  uint8_t flags; // bit0=rapid trigger, bit1=disable kb on gamepad, bit2-4=SOCD resolution
} hid_key_settings_chunk_entry_t;

/**
 * @brief Bulk key settings chunk packet.
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t start_index;
  uint8_t key_count;
  hid_key_settings_chunk_entry_t keys[HID_KEY_SETTINGS_PER_CHUNK];
  uint8_t reserved[12];
} hid_packet_all_keys_t;

/**
 * @brief Gamepad settings packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t keyboard_routing;
  uint8_t square_mode;
  uint8_t reactive_stick;
  uint8_t api_mode;
  struct __attribute__((packed)) {
    uint16_t x_01mm;
    uint8_t y;
  } curve[GAMEPAD_CURVE_POINT_COUNT];
  uint8_t reserved[46];
} hid_packet_gamepad_settings_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t rotation_action;
  uint8_t button_action;
  uint8_t sensitivity;
  uint8_t step_size;
  uint8_t invert_direction;
  uint8_t rgb_behavior;
  uint8_t rgb_effect_mode;
  uint8_t progress_style;
  uint8_t progress_effect_mode;
  uint8_t progress_color_r;
  uint8_t progress_color_g;
  uint8_t progress_color_b;
  uint8_t cw_mode;
  uint16_t cw_keycode;
  uint8_t cw_modifier_mask_exact;
  uint16_t cw_fallback_no_mod_keycode;
  uint8_t cw_layer_mode;
  uint8_t cw_layer_index;
  uint8_t ccw_mode;
  uint16_t ccw_keycode;
  uint8_t ccw_modifier_mask_exact;
  uint16_t ccw_fallback_no_mod_keycode;
  uint8_t ccw_layer_mode;
  uint8_t ccw_layer_index;
  uint8_t click_mode;
  uint16_t click_keycode;
  uint8_t click_modifier_mask_exact;
  uint16_t click_fallback_no_mod_keycode;
  uint8_t click_layer_mode;
  uint8_t click_layer_index;
  uint8_t reserved[26];
} hid_packet_rotary_encoder_settings_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t button_pressed;
  int8_t last_direction;
  uint32_t step_counter;
  uint8_t reserved[56];
} hid_packet_rotary_state_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t profile_index;
  uint8_t profile_used_mask;
  uint8_t reserved[60];
} hid_packet_profile_index_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t source_profile_index;
  uint8_t target_profile_index;
  uint8_t profile_used_mask;
  uint8_t reserved[59];
} hid_packet_profile_copy_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t profile_index;
  uint8_t profile_used_mask;
  char profile_name[SETTINGS_PROFILE_NAME_LENGTH];
  uint8_t reserved[60 - SETTINGS_PROFILE_NAME_LENGTH];
} hid_packet_profile_name_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t layer_index;
  uint8_t key_index;
  uint16_t hid_keycode;
  uint8_t reserved[58];
} hid_packet_layer_keycode_t;

/**
 * @brief Calibration settings packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t start_index;
  uint8_t value_count;
  int16_t lut_zero_value;     // LUT reference zero
  int16_t key_zero_values[HID_CALIBRATION_VALUES_PER_CHUNK];
} hid_packet_calibration_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t start_index;
  uint8_t value_count;
  int16_t lut_zero_value; // echoed for convenience / context
  int16_t key_max_values[HID_CALIBRATION_VALUES_PER_CHUNK];
} hid_packet_calibration_max_t;

/**
 * @brief Auto-calibrate request packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t key_index; // Key to calibrate (0-NUM_KEYS-1), or 0xFF for all
  uint8_t reserved[61];
} hid_packet_auto_calibrate_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t active;
  uint8_t phase;
  uint8_t current_key;
  uint8_t progress_percent;
  uint16_t sample_count;
  uint16_t phase_elapsed_ms;
  uint8_t reserved[54];
} hid_packet_guided_calibration_status_t;

/**
 * @brief ADC values response (debug)
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint16_t adc_raw[6];      // Raw ADC values (12-bit)
  uint16_t adc_filtered[6]; // EMA-filtered ADC values before calibration/LUT
  uint16_t scan_time_us;  // Main loop scan time in microseconds
  uint16_t scan_rate_hz;  // Calculated scan rate in Hz
  uint16_t task_analog_us;
  uint16_t task_trigger_us;
  uint16_t task_socd_us;
  uint16_t task_keyboard_us;
  uint16_t task_keyboard_nkro_us;
  uint16_t task_gamepad_us;
  uint16_t task_led_us;
  uint16_t task_total_us;
  uint16_t analog_raw_us;
  uint16_t analog_filter_us;
  uint16_t analog_calibration_us;
  uint16_t analog_lut_us;
  uint16_t analog_store_us;
  uint16_t analog_key_min_us;
  uint16_t analog_key_max_us;
  uint16_t analog_key_avg_us;
  uint16_t analog_nonzero_keys;
} hid_resp_adc_values_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  int16_t temperature_c;
  uint16_t vref_mv;
  uint32_t core_clock_hz;
  uint16_t scan_cycle_us;
  uint16_t scan_rate_hz;
  uint16_t work_us;
  uint16_t load_permille;
  uint8_t temp_valid;
  uint8_t reserved[45];
} hid_resp_mcu_metrics_t;

/**
 * @brief Key states response (debug)
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t start_index;
  uint8_t key_count;
  struct {
    uint8_t state;          // 0 = released, 1 = pressed
    uint8_t distance_norm;  // 0-255 normalized distance (for bars)
    uint16_t distance_01mm; // Distance in 0.01mm units
  } keys[HID_KEY_STATES_PER_CHUNK];
} hid_resp_key_states_t;

/**
 * @brief ADC capture start request packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t key_index;
  uint8_t reserved0;
  uint32_t duration_ms;
  uint8_t reserved[56];
} hid_req_adc_capture_start_t;

/**
 * @brief ADC capture status response packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t active;
  uint8_t key_index;
  uint32_t duration_ms;
  uint32_t sample_count;
  uint32_t overflow_count;
  uint8_t reserved[48];
} hid_resp_adc_capture_status_t;

/**
 * @brief Request one chunk of raw ADC values.
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t start_index;
  uint8_t reserved[61];
} hid_req_raw_adc_chunk_t;

/**
 * @brief Response chunk containing raw ADC values for a range of keys.
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t start_index;
  uint8_t value_count;
  uint16_t adc_values[HID_RAW_ADC_VALUES_PER_CHUNK];
} hid_resp_adc_chunk_t;

/**
 * @brief ADC capture read request packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint32_t start_index;
  uint8_t max_samples;
  uint8_t reserved[57];
} hid_req_adc_capture_read_t;

/**
 * @brief ADC capture read response packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t active;
  uint8_t key_index;
  uint32_t total_samples;
  uint32_t start_index;
  uint8_t sample_count;
  uint16_t raw_samples[12];
  uint16_t filtered_samples[12];
  uint8_t reserved[3];
} hid_resp_adc_capture_read_t;

/**
 * @brief LED single pixel packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t index;   // LED index (0-LED_MATRIX_SIZE-1)
  uint8_t r, g, b; // RGB values
  uint8_t reserved[58];
} hid_packet_led_pixel_t;

/**
 * @brief LED row packet (8 LEDs = 24 bytes RGB)
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t row;        // Row index (0-7)
  uint8_t pixels[24]; // 8 pixels * 3 bytes (RGB)
  uint8_t reserved[37];
} hid_packet_led_row_t;

/**
 * @brief LED brightness packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t brightness; // 0-255
  uint8_t reserved[61];
} hid_packet_led_brightness_t;

typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t idle_timeout_seconds; // 0 disables idle auto-off
  uint8_t allow_system_when_disabled;
  uint8_t third_party_stream_counts_as_activity;
  uint8_t reserved[59];
} hid_packet_led_idle_options_t;

/**
 * @brief LED fill packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t r, g, b; // Fill color
  uint8_t reserved[59];
} hid_packet_led_fill_t;

/**
 * @brief LED chunk packet for bulk transfer.
 * Used both for GET_LED_ALL and SET_LED_ALL_CHUNK.
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t chunk_index;
  uint8_t chunk_size;               // Bytes in this chunk (HID_LED_BYTES_PER_CHUNK max)
  uint8_t data[HID_LED_BYTES_PER_CHUNK];
} hid_packet_led_chunk_t;

/**
 * @brief Per-key analog curve packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t key_index;     // Key index (0-NUM_KEYS-1)
  uint8_t curve_enabled; // 0 = disabled (linear), 1 = enabled
  uint8_t p1_x;          // Control point 1 X (0-255)
  uint8_t p1_y;          // Control point 1 Y (0-255)
  uint8_t p2_x;          // Control point 2 X (0-255)
  uint8_t p2_y;          // Control point 2 Y (0-255)
  uint8_t reserved[56];
} hid_packet_key_curve_t;

/**
 * @brief Per-key gamepad mapping packet
 */
typedef struct __attribute__((packed)) {
  uint8_t command_id;
  uint8_t status;
  uint8_t key_index; // Key index (0-NUM_KEYS-1)
  uint8_t axis;      // gamepad_axis_t (0=none, 1=LX, 2=LY, etc.)
  uint8_t direction; // 0=positive, 1=negative
  uint8_t button;    // gamepad_button_t (0=none, 1=A, 2=B, etc.)
  uint8_t layer_mask; // Bitmask of active layers (bit0..bit3), 0 => all
  uint8_t reserved[57];
} hid_packet_key_gamepad_map_t;

//--------------------------------------------------------------------+
// Protocol API
//--------------------------------------------------------------------+

/**
 * @brief Initialize the HID protocol handler
 */
void hid_protocol_init(void);

/**
 * @brief Process received HID packet and generate response
 * @param in_packet Input packet (64 bytes)
 * @param out_packet Output packet buffer (64 bytes)
 * @return true if response should be sent
 */
bool hid_protocol_process(const uint8_t *in_packet, uint8_t *out_packet);

#ifdef __cplusplus
}
#endif

#endif /* HID_PROTOCOL_H_ */
