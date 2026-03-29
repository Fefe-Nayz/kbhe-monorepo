from enum import IntEnum

VID = 0x9172
PID = 0x0002
PACKET_SIZE = 64
RAW_HID_USAGE_PAGE = 0xFF00
RAW_HID_INTERFACE = 1

LED_MATRIX_SIZE = 64
LED_MATRIX_WIDTH = 8
LED_MATRIX_HEIGHT = 8


class Command(IntEnum):
    GET_FIRMWARE_VERSION = 0x00
    REBOOT = 0x01
    ENTER_BOOTLOADER = 0x02
    FACTORY_RESET = 0x03

    GET_OPTIONS = 0x20
    SET_OPTIONS = 0x21
    GET_KEYBOARD_ENABLED = 0x22
    SET_KEYBOARD_ENABLED = 0x23
    GET_GAMEPAD_ENABLED = 0x24
    SET_GAMEPAD_ENABLED = 0x25
    SAVE_SETTINGS = 0x26
    GET_NKRO_ENABLED = 0x27
    SET_NKRO_ENABLED = 0x28

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

    GET_FILTER_ENABLED = 0x80
    SET_FILTER_ENABLED = 0x81
    GET_FILTER_PARAMS = 0x82
    SET_FILTER_PARAMS = 0x83

    GET_ADC_VALUES = 0xE0
    GET_KEY_STATES = 0xE1
    GET_LOCK_STATES = 0xE2

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
    "LT": 7,
    "RT": 8,
    "Select": 9,
    "Start": 10,
    "L3": 11,
    "R3": 12,
    "DPad Up": 13,
    "DPad Down": 14,
    "DPad Left": 15,
    "DPad Right": 16,
    "Home": 17,
}
GAMEPAD_BUTTON_NAMES = {value: key for key, value in GAMEPAD_BUTTONS.items()}


HID_KEYCODES = {
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
    'LCTRL': 0xE0, 'LSHIFT': 0xE1, 'LALT': 0xE2, 'LGUI': 0xE3,
    'RCTRL': 0xE4, 'RSHIFT': 0xE5, 'RALT': 0xE6, 'RGUI': 0xE7,
    'MUTE': 0x7F, 'VOLUMEUP': 0x80, 'VOLUMEDOWN': 0x81,
}

HID_KEYCODE_NAMES = {value: key for key, value in HID_KEYCODES.items()}


class Status(IntEnum):
    OK = 0x00
    ERROR = 0x01
    INVALID_CMD = 0x02
    INVALID_PARAM = 0x03
