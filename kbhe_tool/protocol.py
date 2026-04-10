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
LED_EFFECT_PARAM_COUNT = 6
FILTER_DEFAULT_ENABLED = True
FILTER_DEFAULT_NOISE_BAND = 30
FILTER_DEFAULT_ALPHA_MIN_DENOM = 32
FILTER_DEFAULT_ALPHA_MAX_DENOM = 4
GAMEPAD_CURVE_POINT_COUNT = 4
GAMEPAD_CURVE_MAX_DISTANCE_MM = 4.0
LAYER_COUNT = 4
ADVANCED_TICK_RATE_MIN = 1
ADVANCED_TICK_RATE_MAX = 100
ADVANCED_TICK_RATE_DEFAULT = 1


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
    GET_NKRO_ENABLED = 0x27
    SET_NKRO_ENABLED = 0x28
    GET_ADVANCED_TICK_RATE = 0x29
    SET_ADVANCED_TICK_RATE = 0x2A

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
    GET_GAMEPAD_WITH_KB = 0x4D
    SET_GAMEPAD_WITH_KB = 0x4E
    GET_CALIBRATION_MAX = 0x4F
    SET_CALIBRATION_MAX = 0x50
    GUIDED_CALIBRATION_START = 0x51
    GUIDED_CALIBRATION_STATUS = 0x52
    GUIDED_CALIBRATION_ABORT = 0x53
    GET_ROTARY_ENCODER_SETTINGS = 0x54
    SET_ROTARY_ENCODER_SETTINGS = 0x55
    GET_LAYER_KEYCODE = 0x56
    SET_LAYER_KEYCODE = 0x57

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
    GET_LED_EFFECT_SPEED = 0x70
    SET_LED_EFFECT_SPEED = 0x71
    SET_LED_EFFECT_COLOR = 0x72
    GET_LED_FPS_LIMIT = 0x73
    SET_LED_FPS_LIMIT = 0x74
    GET_LED_DIAGNOSTIC = 0x75
    SET_LED_DIAGNOSTIC = 0x76
    GET_LED_EFFECT_PARAMS = 0x77
    SET_LED_EFFECT_PARAMS = 0x78
    SET_LED_VOLUME_OVERLAY = 0x79
    CLEAR_LED_VOLUME_OVERLAY = 0x7A
    GET_LED_EFFECT_COLOR = 0x7C

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
    MATRIX = 0
    RAINBOW = 1
    BREATHING = 2
    STATIC_RAINBOW = 3
    SOLID = 4
    PLASMA = 5
    FIRE = 6
    OCEAN = 7
    MATRIX_RAIN = 8
    SPARKLE = 9
    BREATHING_RAINBOW = 10
    SPIRAL = 11
    COLOR_CYCLE = 12
    REACTIVE = 13
    THIRD_PARTY = 14
    DISTANCE_SENSOR = 15

    # Backward-compatible alias for older code paths.
    NONE = MATRIX


LED_EFFECT_NAMES = {
    LEDEffect.MATRIX: "Matrix (Software)",
    LEDEffect.RAINBOW: "Rainbow Wave",
    LEDEffect.BREATHING: "Breathing",
    LEDEffect.STATIC_RAINBOW: "Static Rainbow",
    LEDEffect.SOLID: "Solid Color",
    LEDEffect.PLASMA: "Plasma",
    LEDEffect.FIRE: "Fire",
    LEDEffect.OCEAN: "Ocean Waves",
    LEDEffect.MATRIX_RAIN: "Matrix Rain",
    LEDEffect.SPARKLE: "Sparkle",
    LEDEffect.BREATHING_RAINBOW: "Breathing Rainbow",
    LEDEffect.SPIRAL: "Spiral",
    LEDEffect.COLOR_CYCLE: "Color Cycle",
    LEDEffect.REACTIVE: "Reactive",
    LEDEffect.THIRD_PARTY: "Third-Party Live",
    LEDEffect.DISTANCE_SENSOR: "Sensor Distance",
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
    "Dynamic Mapping": 3,
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

SOCD_RESOLUTIONS = {
    "Last Input Wins": 0,
    "Most Pressed Wins": 1,
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
