from enum import IntEnum

VID = 0x9172
PID = 0x0002
PACKET_SIZE = 64
RAW_HID_USAGE_PAGE = 0xFF00
RAW_HID_INTERFACE = 1
KEY_COUNT = 82
KEY_SETTINGS_PER_CHUNK = 6
CALIBRATION_VALUES_PER_CHUNK = 29
KEY_STATES_PER_CHUNK = 15
LED_BYTES_PER_CHUNK = 60

# The firmware stores one RGB triplet per logical key/LED.
LED_MATRIX_SIZE = KEY_COUNT
LED_MATRIX_WIDTH = 8
LED_MATRIX_HEIGHT = 8
LED_EFFECT_PARAM_COUNT = 16
LED_EFFECT_PARAM_COLOR_R = 8
LED_EFFECT_PARAM_COLOR_G = 9
LED_EFFECT_PARAM_COLOR_B = 10
LED_EFFECT_PARAM_COLOR2_R = 11
LED_EFFECT_PARAM_COLOR2_G = 12
LED_EFFECT_PARAM_COLOR2_B = 13
LED_EFFECT_PARAM_SPEED = 14
LED_AUDIO_SPECTRUM_BAND_COUNT = 16
FILTER_DEFAULT_ENABLED = True
FILTER_DEFAULT_NOISE_BAND = 30
FILTER_DEFAULT_ALPHA_MIN_DENOM = 32
FILTER_DEFAULT_ALPHA_MAX_DENOM = 4
GAMEPAD_CURVE_POINT_COUNT = 4
GAMEPAD_CURVE_MAX_DISTANCE_MM = 4.0
LAYER_COUNT = 4
SETTINGS_PROFILE_COUNT = 4
SETTINGS_PROFILE_NAME_LENGTH = 16
ADVANCED_TICK_RATE_MIN = 1
ADVANCED_TICK_RATE_MAX = 100
ADVANCED_TICK_RATE_DEFAULT = 1
TRIGGER_CHATTER_GUARD_DEFAULT_ENABLED = False
TRIGGER_CHATTER_GUARD_DEFAULT_MS = 0
TRIGGER_CHATTER_GUARD_MAX_MS = 20
KEYBOARD_NAME_LENGTH = 32
DEVICE_SERIAL_LENGTH = 26


class Command(IntEnum):
    GET_FIRMWARE_VERSION = 0x00
    REBOOT = 0x01
    ENTER_BOOTLOADER = 0x02
    FACTORY_RESET = 0x03
    USB_REENUMERATE = 0x04

    GET_OPTIONS = 0x20
    SET_OPTIONS = 0x21
    GET_KEYBOARD_ENABLED = 0x22
    SET_KEYBOARD_ENABLED = 0x23
    GET_GAMEPAD_ENABLED = 0x24
    SET_GAMEPAD_ENABLED = 0x25
    SAVE_SETTINGS = 0x26
    GET_NKRO_ENABLED = 0x27  # True=Auto NKRO, False=6KRO only
    SET_NKRO_ENABLED = 0x28  # True=Auto NKRO, False=6KRO only
    GET_ADVANCED_TICK_RATE = 0x29
    SET_ADVANCED_TICK_RATE = 0x2A
    GET_DEVICE_INFO = 0x2B
    GET_KEYBOARD_NAME = 0x2C
    SET_KEYBOARD_NAME = 0x2D
    COPY_PROFILE_SLOT = 0x2E
    RESET_PROFILE_SLOT = 0x2F
    GET_DEFAULT_PROFILE = 0x30
    SET_DEFAULT_PROFILE = 0x31
    GET_RAM_ONLY_MODE = 0x32
    SET_RAM_ONLY_MODE = 0x33
    RELOAD_SETTINGS_FROM_FLASH = 0x34
    GET_TRIGGER_CHATTER_GUARD = 0x35
    SET_TRIGGER_CHATTER_GUARD = 0x36

    GET_KEY_SETTINGS = 0x40
    SET_KEY_SETTINGS = 0x41
    GET_ALL_KEY_SETTINGS = 0x42
    SET_ALL_KEY_SETTINGS = 0x43
    GET_GAMEPAD_SETTINGS = 0x44
    SET_GAMEPAD_SETTINGS = 0x45
    GET_CALIBRATION = 0x46
    SET_CALIBRATION = 0x47
    AUTO_CALIBRATE = 0x48
    GET_KEY_CURVE = 0x49
    SET_KEY_CURVE = 0x4A
    GET_KEY_GAMEPAD_MAP = 0x4B
    SET_KEY_GAMEPAD_MAP = 0x4C
    GET_CALIBRATION_MAX = 0x4F
    SET_CALIBRATION_MAX = 0x50
    GUIDED_CALIBRATION_START = 0x51
    GUIDED_CALIBRATION_STATUS = 0x52
    GUIDED_CALIBRATION_ABORT = 0x53
    GET_ROTARY_ENCODER_SETTINGS = 0x54
    SET_ROTARY_ENCODER_SETTINGS = 0x55
    GET_LAYER_KEYCODE = 0x56
    SET_LAYER_KEYCODE = 0x57
    RESET_KEY_TRIGGER_SETTINGS = 0x58
    GET_ROTARY_STATE = 0x59
    GET_ACTIVE_PROFILE = 0x5A
    SET_ACTIVE_PROFILE = 0x5B
    GET_PROFILE_NAME = 0x5C
    SET_PROFILE_NAME = 0x5D
    CREATE_PROFILE = 0x5E
    DELETE_PROFILE = 0x5F

    GET_LED_ENABLED = 0x60
    SET_LED_ENABLED = 0x61
    GET_LED_BRIGHTNESS = 0x62
    SET_LED_BRIGHTNESS = 0x63
    GET_LED_PIXEL = 0x64
    SET_LED_PIXEL = 0x65
    GET_LED_ROW = 0x66
    SET_LED_ROW = 0x67
    GET_LED_ALL = 0x68
    SET_LED_ALL = 0x69
    SET_LED_ALL_CHUNK = 0x6A
    LED_CLEAR = 0x6B
    LED_FILL = 0x6C
    LED_TEST_RAINBOW = 0x6D
    GET_LED_EFFECT = 0x6E
    SET_LED_EFFECT = 0x6F
    GET_LED_FPS_LIMIT = 0x70
    SET_LED_FPS_LIMIT = 0x71
    GET_LED_EFFECT_PARAMS = 0x72
    SET_LED_EFFECT_PARAMS = 0x73
    SET_LED_VOLUME_OVERLAY = 0x74
    CLEAR_LED_VOLUME_OVERLAY = 0x75
    RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY = 0x76
    GET_LED_EFFECT_SCHEMA = 0x77
    SET_LED_AUDIO_SPECTRUM = 0x78
    CLEAR_LED_AUDIO_SPECTRUM = 0x79

    GET_FILTER_ENABLED = 0x80
    SET_FILTER_ENABLED = 0x81
    GET_FILTER_PARAMS = 0x82
    SET_FILTER_PARAMS = 0x83

    GET_ADC_VALUES = 0xE0
    GET_KEY_STATES = 0xE1
    GET_LOCK_STATES = 0xE2
    ADC_CAPTURE_START = 0xE3
    ADC_CAPTURE_STATUS = 0xE4
    ADC_CAPTURE_READ = 0xE5
    GET_RAW_ADC_CHUNK = 0xE6
    GET_FILTERED_ADC_CHUNK = 0xE7
    GET_CALIBRATED_ADC_CHUNK = 0xE8
    GET_MCU_METRICS = 0xE9

    ECHO = 0xFE


class LEDEffect(IntEnum):
    # Compact IDs after Phase-1B fusion. Old IDs documented in
    # docs/LED_EFFECTS_CANONICAL_PHASE0.md (breaking change with v0x0017+).
    NONE = 0              # Static matrix (software pattern)
    PLASMA = 1
    FIRE = 2
    OCEAN = 3
    SPARKLE = 4
    BREATHING_RAINBOW = 5
    COLOR_CYCLE = 6
    THIRD_PARTY = 7
    DISTANCE_SENSOR = 8
    IMPACT_RAINBOW = 9
    REACTIVE_GHOST = 10
    AUDIO_SPECTRUM = 11
    KEY_STATE_DEMO = 12
    CYCLE_PINWHEEL = 13
    CYCLE_SPIRAL = 14           # fused: old SPIRAL
    CYCLE_OUT_IN_DUAL = 15      # fused: DUAL_SPHERE
    RAINBOW_BEACON = 16         # fused: STRIP_SPIN_ZOOM
    RAINBOW_PINWHEELS = 17
    RAINBOW_MOVING_CHEVRON = 18
    HUE_BREATHING = 19
    HUE_PENDULUM = 20
    HUE_WAVE = 21
    RIVERFLOW = 22
    SOLID_COLOR = 23            # fused: old SOLID
    ALPHA_MODS = 24
    GRADIENT_UP_DOWN = 25
    GRADIENT_LEFT_RIGHT = 26    # fused: STATIC_RAINBOW
    BREATHING = 27              # fused: old BREATHING
    COLORBAND_SAT = 28
    COLORBAND_VAL = 29
    COLORBAND_PINWHEEL_SAT = 30
    COLORBAND_PINWHEEL_VAL = 31
    COLORBAND_SPIRAL_SAT = 32
    COLORBAND_SPIRAL_VAL = 33
    CYCLE_ALL = 34
    CYCLE_LEFT_RIGHT = 35       # fused: old RAINBOW
    CYCLE_UP_DOWN = 36
    CYCLE_OUT_IN = 37           # fused: SPHERE
    DUAL_BEACON = 38
    FLOWER_BLOOMING = 39
    RAINDROPS = 40
    JELLYBEAN_RAINDROPS = 41
    PIXEL_RAIN = 42
    PIXEL_FLOW = 43
    PIXEL_FRACTAL = 44
    TYPING_HEATMAP = 45         # fused: REACTIVE_HEATMAP
    DIGITAL_RAIN = 46           # fused: old MATRIX
    SOLID_REACTIVE_SIMPLE = 47
    SOLID_REACTIVE = 48
    SOLID_REACTIVE_WIDE = 49
    SOLID_REACTIVE_CROSS = 50
    SOLID_REACTIVE_NEXUS = 51
    SPLASH = 52                 # fused: old REACTIVE
    SOLID_SPLASH = 53
    STARLIGHT_SMOOTH = 54
    STARLIGHT = 55
    STARLIGHT_DUAL_SAT = 56
    STARLIGHT_DUAL_HUE = 57
    SOLID_REACTIVE_MULTI_WIDE = 58
    SOLID_REACTIVE_MULTI_CROSS = 59
    SOLID_REACTIVE_MULTI_NEXUS = 60
    MULTI_SPLASH = 61
    SOLID_MULTI_SPLASH = 62

LED_EFFECT_NAMES = {
    LEDEffect.NONE: "Matrix (Software)",
    LEDEffect.PLASMA: "Plasma",
    LEDEffect.FIRE: "Fire",
    LEDEffect.OCEAN: "Ocean Waves",
    LEDEffect.SPARKLE: "Sparkle",
    LEDEffect.BREATHING_RAINBOW: "Breathing Rainbow",
    LEDEffect.COLOR_CYCLE: "Color Cycle",
    LEDEffect.THIRD_PARTY: "Third-Party Live",
    LEDEffect.DISTANCE_SENSOR: "Sensor Distance",
    LEDEffect.IMPACT_RAINBOW: "Impact Rainbow",
    LEDEffect.REACTIVE_GHOST: "Reactive Ghost",
    LEDEffect.AUDIO_SPECTRUM: "Audio Spectrum",
    LEDEffect.KEY_STATE_DEMO: "Key State Demo",
    LEDEffect.CYCLE_PINWHEEL: "Cycle Pinwheel",
    LEDEffect.CYCLE_SPIRAL: "Cycle Spiral",
    LEDEffect.CYCLE_OUT_IN_DUAL: "Cycle Out-In Dual",
    LEDEffect.RAINBOW_BEACON: "Rainbow Beacon",
    LEDEffect.RAINBOW_PINWHEELS: "Rainbow Pinwheels",
    LEDEffect.RAINBOW_MOVING_CHEVRON: "Rainbow Moving Chevron",
    LEDEffect.HUE_BREATHING: "Hue Breathing",
    LEDEffect.HUE_PENDULUM: "Hue Pendulum",
    LEDEffect.HUE_WAVE: "Hue Wave",
    LEDEffect.RIVERFLOW: "Riverflow",
    LEDEffect.SOLID_COLOR: "Solid Color",
    LEDEffect.ALPHA_MODS: "Alpha Mods",
    LEDEffect.GRADIENT_UP_DOWN: "Gradient Up-Down",
    LEDEffect.GRADIENT_LEFT_RIGHT: "Gradient Left-Right",
    LEDEffect.BREATHING: "Breathing",
    LEDEffect.COLORBAND_SAT: "Colorband Sat",
    LEDEffect.COLORBAND_VAL: "Colorband Val",
    LEDEffect.COLORBAND_PINWHEEL_SAT: "Colorband Pinwheel Sat",
    LEDEffect.COLORBAND_PINWHEEL_VAL: "Colorband Pinwheel Val",
    LEDEffect.COLORBAND_SPIRAL_SAT: "Colorband Spiral Sat",
    LEDEffect.COLORBAND_SPIRAL_VAL: "Colorband Spiral Val",
    LEDEffect.CYCLE_ALL: "Cycle All",
    LEDEffect.CYCLE_LEFT_RIGHT: "Cycle Left-Right",
    LEDEffect.CYCLE_UP_DOWN: "Cycle Up-Down",
    LEDEffect.CYCLE_OUT_IN: "Cycle Out-In",
    LEDEffect.DUAL_BEACON: "Dual Beacon",
    LEDEffect.FLOWER_BLOOMING: "Flower Blooming",
    LEDEffect.RAINDROPS: "Raindrops",
    LEDEffect.JELLYBEAN_RAINDROPS: "Jellybean Raindrops",
    LEDEffect.PIXEL_RAIN: "Pixel Rain",
    LEDEffect.PIXEL_FLOW: "Pixel Flow",
    LEDEffect.PIXEL_FRACTAL: "Pixel Fractal",
    LEDEffect.TYPING_HEATMAP: "Typing Heatmap",
    LEDEffect.DIGITAL_RAIN: "Digital Rain",
    LEDEffect.SOLID_REACTIVE_SIMPLE: "Solid Reactive Simple",
    LEDEffect.SOLID_REACTIVE: "Solid Reactive",
    LEDEffect.SOLID_REACTIVE_WIDE: "Solid Reactive Wide",
    LEDEffect.SOLID_REACTIVE_CROSS: "Solid Reactive Cross",
    LEDEffect.SOLID_REACTIVE_NEXUS: "Solid Reactive Nexus",
    LEDEffect.SPLASH: "Splash",
    LEDEffect.SOLID_SPLASH: "Solid Splash",
    LEDEffect.STARLIGHT_SMOOTH: "Starlight Smooth",
    LEDEffect.STARLIGHT: "Starlight",
    LEDEffect.STARLIGHT_DUAL_SAT: "Starlight Dual Sat",
    LEDEffect.STARLIGHT_DUAL_HUE: "Starlight Dual Hue",
    LEDEffect.SOLID_REACTIVE_MULTI_WIDE: "Solid Reactive Multi Wide",
    LEDEffect.SOLID_REACTIVE_MULTI_CROSS: "Solid Reactive Multi Cross",
    LEDEffect.SOLID_REACTIVE_MULTI_NEXUS: "Solid Reactive Multi Nexus",
    LEDEffect.MULTI_SPLASH: "Multi Splash",
    LEDEffect.SOLID_MULTI_SPLASH: "Solid Multi Splash",
}


# ---------------------------------------------------------------------------
# Effect parameter schema (Phase-2A)
# ---------------------------------------------------------------------------

class ParamType(IntEnum):
    NONE  = 0  # Slot unused by this effect
    U8    = 1  # Generic 0-255 integer
    BOOL  = 2  # Boolean flag (0 or 1)
    HUE   = 3  # Hue wheel 0-255
    COLOR = 4  # First byte of RGB triplet; host groups id, id+1, id+2 as one picker

# Number of param descriptors that fit in a single schema HID response chunk.
SCHEMA_PARAMS_PER_CHUNK = 9
# Byte size of one packed descriptor on the wire.
SCHEMA_DESCRIPTOR_BYTES = 6  # [id, type, min, max, default_val, step]

# Parse the payload of a CMD_GET_LED_EFFECT_SCHEMA response into a list of
# descriptor dicts.  Raises ValueError on malformed input.
def parse_schema_chunk(payload: bytes) -> dict:
    """
    payload = response packet bytes starting at payload[0] (after cmd_id/status).
    Returns:
      {
        "effect_id":    int,
        "chunk_index":  int,
        "total_chunks": int,
        "total_active": int,
        "descriptors":  [{"id", "type", "min", "max", "default", "step"}, ...]
      }
    """
    if len(payload) < 4:
        raise ValueError("Schema response payload too short")
    effect_id    = payload[0]
    chunk_index  = payload[1]
    total_chunks = payload[2]
    total_active = payload[3]
    raw = payload[4:]
    if len(raw) % SCHEMA_DESCRIPTOR_BYTES != 0:
        raise ValueError("Schema payload length not a multiple of descriptor size")
    descriptors = []
    for i in range(0, len(raw), SCHEMA_DESCRIPTOR_BYTES):
        b = raw[i:i + SCHEMA_DESCRIPTOR_BYTES]
        if len(b) < SCHEMA_DESCRIPTOR_BYTES:
            break
        descriptors.append({
            "id":      b[0],
            "type":    ParamType(b[1]),
            "min":     b[2],
            "max":     b[3],
            "default": b[4],
            "step":    b[5],
        })
    return {
        "effect_id":    effect_id,
        "chunk_index":  chunk_index,
        "total_chunks": total_chunks,
        "total_active": total_active,
        "descriptors":  descriptors,
    }


GAMEPAD_AXES = {
    "None": 0,
    "Left Stick X": 1,
    "Left Stick Y": 2,
    "Right Stick X": 3,
    "Right Stick Y": 4,
    "Trigger L": 5,
    "Trigger R": 6,
}
GAMEPAD_AXIS_NAMES = {value: key for key, value in GAMEPAD_AXES.items()}

GAMEPAD_DIRECTIONS = {
    "+": 0,
    "-": 1,
}
GAMEPAD_DIRECTION_NAMES = {value: key for key, value in GAMEPAD_DIRECTIONS.items()}

GAMEPAD_BUTTONS = {
    "None": 0,
    "A": 1,
    "B": 2,
    "X": 3,
    "Y": 4,
    "LB": 5,
    "RB": 6,
    "LT (Full Trigger)": 7,
    "RT (Full Trigger)": 8,
    "Back / Minus": 9,
    "Start / Plus": 10,
    "L3": 11,
    "R3": 12,
    "DPad Up": 13,
    "DPad Down": 14,
    "DPad Left": 15,
    "DPad Right": 16,
    "Guide / Home": 17,
}
GAMEPAD_BUTTON_NAMES = {value: key for key, value in GAMEPAD_BUTTONS.items()}

GAMEPAD_KEYBOARD_ROUTING = {
    "Disabled": 0,
    "All Keys": 1,
    "Unmapped Only": 2,
}
GAMEPAD_KEYBOARD_ROUTING_NAMES = {
    value: key for key, value in GAMEPAD_KEYBOARD_ROUTING.items()
}

GAMEPAD_API_MODES = {
    "HID (DirectInput)": 0,
    "XInput (Xbox Compatible)": 1,
}
GAMEPAD_API_MODE_NAMES = {value: key for key, value in GAMEPAD_API_MODES.items()}

KEY_BEHAVIORS = {
    "Normal": 0,
    "Tap-Hold": 1,
    "Toggle": 2,
    "Dynamic Keystroke": 3,
}
KEY_BEHAVIOR_NAMES = {value: key for key, value in KEY_BEHAVIORS.items()}


ROTARY_ACTIONS = {
    "Volume": 0,
    "LED Brightness": 1,
    "Effect Speed": 2,
    "Effect Cycle": 3,
    "RGB Customizer": 4,
}
ROTARY_ACTION_NAMES = {value: key for key, value in ROTARY_ACTIONS.items()}

ROTARY_BUTTON_ACTIONS = {
    "Play / Pause": 0,
    "Mute": 1,
    "Toggle LEDs": 2,
    "Next Effect": 3,
    "Cycle Rotary Action": 4,
}
ROTARY_BUTTON_ACTION_NAMES = {
    value: key for key, value in ROTARY_BUTTON_ACTIONS.items()
}

ROTARY_RGB_BEHAVIORS = {
    "Hue": 0,
    "Brightness": 1,
    "Effect Speed": 2,
    "Effect Cycle": 3,
}
ROTARY_RGB_BEHAVIOR_NAMES = {
    value: key for key, value in ROTARY_RGB_BEHAVIORS.items()
}

ROTARY_PROGRESS_STYLES = {
    "Solid Color": 0,
    "Rainbow Bar": 1,
    "Effect Palette": 2,
}
ROTARY_PROGRESS_STYLE_NAMES = {
    value: key for key, value in ROTARY_PROGRESS_STYLES.items()
}

ROTARY_BINDING_MODES = {
    "Internal Action": 0,
    "Keycode": 1,
}
ROTARY_BINDING_MODE_NAMES = {
    value: key for key, value in ROTARY_BINDING_MODES.items()
}

ROTARY_BINDING_LAYER_MODES = {
    "Active Layer": 0,
    "Fixed Layer": 1,
}
ROTARY_BINDING_LAYER_MODE_NAMES = {
    value: key for key, value in ROTARY_BINDING_LAYER_MODES.items()
}

SOCD_RESOLUTIONS = {
    "Last Input Wins": 0,
    "Most Pressed Wins": 1,
    "Absolute Priority (Key 1)": 2,
    "Absolute Priority (Key 2)": 3,
    "Neutral": 4,
}
SOCD_RESOLUTION_NAMES = {value: key for key, value in SOCD_RESOLUTIONS.items()}

LAYER_NAMES = {
    0: "Base",
    1: "Fn",
    2: "Layer 2",
    3: "Layer 3",
}


HID_KEYCODES = {
    'NO': 0x0000,
    'TRANSPARENT': 0x0001,
    'A': 0x04, 'B': 0x05, 'C': 0x06, 'D': 0x07, 'E': 0x08, 'F': 0x09,
    'G': 0x0A, 'H': 0x0B, 'I': 0x0C, 'J': 0x0D, 'K': 0x0E, 'L': 0x0F,
    'M': 0x10, 'N': 0x11, 'O': 0x12, 'P': 0x13, 'Q': 0x14, 'R': 0x15,
    'S': 0x16, 'T': 0x17, 'U': 0x18, 'V': 0x19, 'W': 0x1A, 'X': 0x1B,
    'Y': 0x1C, 'Z': 0x1D,
    '1': 0x1E, '2': 0x1F, '3': 0x20, '4': 0x21, '5': 0x22,
    '6': 0x23, '7': 0x24, '8': 0x25, '9': 0x26, '0': 0x27,
    'ENTER': 0x28, 'ESC': 0x29, 'BACKSPACE': 0x2A, 'TAB': 0x2B,
    'SPACE': 0x2C, 'MINUS': 0x2D, 'EQUAL': 0x2E, 'LEFTBRACE': 0x2F,
    'RIGHTBRACE': 0x30, 'BACKSLASH': 0x31, 'SEMICOLON': 0x33,
    'APOSTROPHE': 0x34, 'GRAVE': 0x35, 'COMMA': 0x36, 'DOT': 0x37,
    'SLASH': 0x38,
    'CAPSLOCK': 0x39, 'NUMLOCK': 0x53, 'SCROLLLOCK': 0x47,
    'F1': 0x3A, 'F2': 0x3B, 'F3': 0x3C, 'F4': 0x3D, 'F5': 0x3E, 'F6': 0x3F,
    'F7': 0x40, 'F8': 0x41, 'F9': 0x42, 'F10': 0x43, 'F11': 0x44, 'F12': 0x45,
    'PRINTSCREEN': 0x46, 'PAUSE': 0x48, 'INSERT': 0x49,
    'HOME': 0x4A, 'PAGEUP': 0x4B, 'DELETE': 0x4C, 'END': 0x4D,
    'PAGEDOWN': 0x4E, 'RIGHT': 0x4F, 'LEFT': 0x50, 'DOWN': 0x51, 'UP': 0x52,
    'KP_DIVIDE': 0x54, 'KP_MULTIPLY': 0x55, 'KP_MINUS': 0x56, 'KP_PLUS': 0x57,
    'KP_ENTER': 0x58, 'KP_1': 0x59, 'KP_2': 0x5A, 'KP_3': 0x5B, 'KP_4': 0x5C,
    'KP_5': 0x5D, 'KP_6': 0x5E, 'KP_7': 0x5F, 'KP_8': 0x60, 'KP_9': 0x61,
    'KP_0': 0x62, 'KP_DOT': 0x63,
    'NONUS_BACKSLASH': 0x64, 'APPLICATION': 0x65, 'KB_POWER': 0x66, 'KP_EQUAL': 0x67,
    'F13': 0x68, 'F14': 0x69, 'F15': 0x6A, 'F16': 0x6B, 'F17': 0x6C, 'F18': 0x6D,
    'F19': 0x6E, 'F20': 0x6F, 'F21': 0x70, 'F22': 0x71, 'F23': 0x72, 'F24': 0x73,
    'EXECUTE': 0x74, 'HELP': 0x75, 'MENU': 0x76, 'SELECT': 0x77, 'STOP': 0x78,
    'AGAIN': 0x79, 'UNDO': 0x7A, 'CUT': 0x7B, 'COPY': 0x7C, 'PASTE': 0x7D, 'FIND': 0x7E,
    'LCTRL': 0xE0, 'LSHIFT': 0xE1, 'LALT': 0xE2, 'LGUI': 0xE3,
    'RCTRL': 0xE4, 'RSHIFT': 0xE5, 'RALT': 0xE6, 'RGUI': 0xE7,
    'MUTE': 0x7F, 'VOLUMEUP': 0x80, 'VOLUMEDOWN': 0x81,
    'AUDIO_MUTE': 0x00A8, 'AUDIO_VOL_UP': 0x00A9, 'AUDIO_VOL_DOWN': 0x00AA,
    'MEDIA_NEXT_TRACK': 0x00AB, 'MEDIA_PREV_TRACK': 0x00AC, 'MEDIA_STOP': 0x00AD,
    'MEDIA_PLAY_PAUSE': 0x00AE, 'MEDIA_SELECT': 0x00AF, 'MEDIA_EJECT': 0x00B0,
    'MAIL': 0x00B1, 'CALCULATOR': 0x00B2, 'MY_COMPUTER': 0x00B3,
    'WWW_SEARCH': 0x00B4, 'WWW_HOME': 0x00B5, 'WWW_BACK': 0x00B6, 'WWW_FORWARD': 0x00B7,
    'WWW_STOP': 0x00B8, 'WWW_REFRESH': 0x00B9, 'WWW_FAVORITES': 0x00BA,
    'MEDIA_FAST_FORWARD': 0x00BB, 'MEDIA_REWIND': 0x00BC,
    'BRIGHTNESS_UP': 0x00BD, 'BRIGHTNESS_DOWN': 0x00BE, 'CONTROL_PANEL': 0x00BF,
    'FN / MO Layer 1': 0xF000, 'MO Layer 2': 0xF001, 'MO Layer 3': 0xF002,
    'TG Layer 1': 0xF010, 'TG Layer 2': 0xF011, 'TG Layer 3': 0xF012,
    'Set Base Layer': 0xF018, 'Set Fn Layer': 0xF019,
    'Set Layer 2': 0xF01A, 'Set Layer 3': 0xF01B, 'Clear Layer Toggles': 0xF01C,
    'Prev Profile': 0xF020, 'Next Profile': 0xF021,
    'Set Profile 1': 0xF022, 'Set Profile 2': 0xF023,
    'Set Profile 3': 0xF024, 'Set Profile 4': 0xF025,
    'LED Toggle': 0xF200, 'LED Brightness Down': 0xF201, 'LED Brightness Up': 0xF202,
    'LED Effect Prev': 0xF203, 'LED Effect Next': 0xF204,
    'LED Speed Down': 0xF205, 'LED Speed Up': 0xF206, 'LED Color Next': 0xF207,
    'Gamepad Enable': 0xF300, 'Gamepad Disable': 0xF301, 'Gamepad Toggle': 0xF302,
    'GP A': 0xF320, 'GP B': 0xF321, 'GP X': 0xF322, 'GP Y': 0xF323,
    'GP LB': 0xF324, 'GP RB': 0xF325, 'GP LT Trigger': 0xF326, 'GP RT Trigger': 0xF327,
    'GP Back': 0xF328, 'GP Start': 0xF329, 'GP L3': 0xF32A, 'GP R3': 0xF32B,
    'GP DPad Up': 0xF32C, 'GP DPad Down': 0xF32D,
    'GP DPad Left': 0xF32E, 'GP DPad Right': 0xF32F, 'GP Home': 0xF330,
    'GP LS Right': 0xF340, 'GP LS Left': 0xF341, 'GP LS Down': 0xF342, 'GP LS Up': 0xF343,
    'GP RS Right': 0xF344, 'GP RS Left': 0xF345, 'GP RS Down': 0xF346, 'GP RS Up': 0xF347,
    'MOUSE_LEFT': 0xF100, 'MOUSE_RIGHT': 0xF101, 'MOUSE_MIDDLE': 0xF102,
    'MOUSE_BACK': 0xF103, 'MOUSE_FORWARD': 0xF104,
    'MOUSE_WHEEL_UP': 0xF105, 'MOUSE_WHEEL_DOWN': 0xF106,
    'MOUSE_WHEEL_LEFT': 0xF107, 'MOUSE_WHEEL_RIGHT': 0xF108,
    'FN': 0xF000,
}

HID_KEYCODE_NAMES = {value: key for key, value in HID_KEYCODES.items()}
HID_KEYCODE_NAMES[0xF000] = "FN / MO Layer 1"


class Status(IntEnum):
    OK = 0x00
    ERROR = 0x01
    INVALID_CMD = 0x02
    INVALID_PARAM = 0x03
