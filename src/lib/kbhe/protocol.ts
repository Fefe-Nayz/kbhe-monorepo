export const VID = 0x9172;
export const PID = 0x0002;
export const UPDATER_PID = 0x0003;
export const PACKET_SIZE = 64;
export const REPORT_SIZE = PACKET_SIZE + 1;
export const REPORT_ID = 0x00;
export const RAW_HID_USAGE_PAGE = 0xff00;
export const RAW_HID_INTERFACE = 1;
export const KEY_COUNT = 82;
export const KEY_SETTINGS_PER_CHUNK = 6;
export const CALIBRATION_VALUES_PER_CHUNK = 29;
export const KEY_STATES_PER_CHUNK = 15;
export const LED_BYTES_PER_CHUNK = 60;
export const LED_MATRIX_SIZE = KEY_COUNT;
export const LED_MATRIX_WIDTH = 8;
export const LED_MATRIX_HEIGHT = 8;
export const LED_EFFECT_PARAM_COUNT = 6;
export const FILTER_DEFAULT_ENABLED = true;
export const FILTER_DEFAULT_NOISE_BAND = 30;
export const FILTER_DEFAULT_ALPHA_MIN_DENOM = 32;
export const FILTER_DEFAULT_ALPHA_MAX_DENOM = 4;
export const GAMEPAD_CURVE_POINT_COUNT = 4;
export const GAMEPAD_CURVE_MAX_DISTANCE_MM = 4.0;
export const LAYER_COUNT = 4;
export const ADVANCED_TICK_RATE_MIN = 1;
export const ADVANCED_TICK_RATE_MAX = 100;
export const ADVANCED_TICK_RATE_DEFAULT = 1;
export const DEVICE_SERIAL_MAX_LENGTH = 26;
export const KEYBOARD_NAME_LENGTH = 32;

export enum Command {
  GET_FIRMWARE_VERSION = 0x00,
  REBOOT = 0x01,
  ENTER_BOOTLOADER = 0x02,
  FACTORY_RESET = 0x03,
  USB_REENUMERATE = 0x04,

  GET_OPTIONS = 0x20,
  SET_OPTIONS = 0x21,
  GET_KEYBOARD_ENABLED = 0x22,
  SET_KEYBOARD_ENABLED = 0x23,
  GET_GAMEPAD_ENABLED = 0x24,
  SET_GAMEPAD_ENABLED = 0x25,
  SAVE_SETTINGS = 0x26,
  GET_NKRO_ENABLED = 0x27,
  SET_NKRO_ENABLED = 0x28,
  GET_ADVANCED_TICK_RATE = 0x29,
  SET_ADVANCED_TICK_RATE = 0x2a,
  GET_DEVICE_INFO = 0x2b,
  GET_KEYBOARD_NAME = 0x2c,
  SET_KEYBOARD_NAME = 0x2d,

  GET_KEY_SETTINGS = 0x40,
  SET_KEY_SETTINGS = 0x41,
  GET_ALL_KEY_SETTINGS = 0x42,
  SET_ALL_KEY_SETTINGS = 0x43,
  GET_GAMEPAD_SETTINGS = 0x44,
  SET_GAMEPAD_SETTINGS = 0x45,
  GET_CALIBRATION = 0x46,
  SET_CALIBRATION = 0x47,
  AUTO_CALIBRATE = 0x48,
  GET_KEY_CURVE = 0x49,
  SET_KEY_CURVE = 0x4a,
  GET_KEY_GAMEPAD_MAP = 0x4b,
  SET_KEY_GAMEPAD_MAP = 0x4c,
  GET_GAMEPAD_WITH_KB = 0x4d,
  SET_GAMEPAD_WITH_KB = 0x4e,
  GET_CALIBRATION_MAX = 0x4f,
  SET_CALIBRATION_MAX = 0x50,
  GUIDED_CALIBRATION_START = 0x51,
  GUIDED_CALIBRATION_STATUS = 0x52,
  GUIDED_CALIBRATION_ABORT = 0x53,
  GET_ROTARY_ENCODER_SETTINGS = 0x54,
  SET_ROTARY_ENCODER_SETTINGS = 0x55,
  GET_LAYER_KEYCODE = 0x56,
  SET_LAYER_KEYCODE = 0x57,

  GET_LED_ENABLED = 0x60,
  SET_LED_ENABLED = 0x61,
  GET_LED_BRIGHTNESS = 0x62,
  SET_LED_BRIGHTNESS = 0x63,
  GET_LED_PIXEL = 0x64,
  SET_LED_PIXEL = 0x65,
  GET_LED_ROW = 0x66,
  SET_LED_ROW = 0x67,
  GET_LED_ALL = 0x68,
  SET_LED_ALL = 0x69,
  SET_LED_ALL_CHUNK = 0x6a,
  LED_CLEAR = 0x6b,
  LED_FILL = 0x6c,
  LED_TEST_RAINBOW = 0x6d,
  GET_LED_EFFECT = 0x6e,
  SET_LED_EFFECT = 0x6f,
  GET_LED_EFFECT_SPEED = 0x70,
  SET_LED_EFFECT_SPEED = 0x71,
  SET_LED_EFFECT_COLOR = 0x72,
  GET_LED_FPS_LIMIT = 0x73,
  SET_LED_FPS_LIMIT = 0x74,
  GET_LED_DIAGNOSTIC = 0x75,
  SET_LED_DIAGNOSTIC = 0x76,
  GET_LED_EFFECT_PARAMS = 0x77,
  SET_LED_EFFECT_PARAMS = 0x78,
  SET_LED_VOLUME_OVERLAY = 0x79,
  CLEAR_LED_VOLUME_OVERLAY = 0x7a,
  GET_LED_EFFECT_COLOR = 0x7c,

  GET_FILTER_ENABLED = 0x80,
  SET_FILTER_ENABLED = 0x81,
  GET_FILTER_PARAMS = 0x82,
  SET_FILTER_PARAMS = 0x83,

  GET_ADC_VALUES = 0xe0,
  GET_KEY_STATES = 0xe1,
  GET_LOCK_STATES = 0xe2,
  ADC_CAPTURE_START = 0xe3,
  ADC_CAPTURE_STATUS = 0xe4,
  ADC_CAPTURE_READ = 0xe5,
  GET_RAW_ADC_CHUNK = 0xe6,
  GET_FILTERED_ADC_CHUNK = 0xe7,
  GET_CALIBRATED_ADC_CHUNK = 0xe8,
  GET_MCU_METRICS = 0xe9,

  ECHO = 0xfe,
}

export enum Status {
  OK = 0x00,
  ERROR = 0x01,
  INVALID_CMD = 0x02,
  INVALID_PARAM = 0x03,
}

export enum LEDEffect {
  MATRIX = 0,
  RAINBOW = 1,
  BREATHING = 2,
  STATIC_RAINBOW = 3,
  SOLID = 4,
  PLASMA = 5,
  FIRE = 6,
  OCEAN = 7,
  MATRIX_RAIN = 8,
  SPARKLE = 9,
  BREATHING_RAINBOW = 10,
  SPIRAL = 11,
  COLOR_CYCLE = 12,
  REACTIVE = 13,
  THIRD_PARTY = 14,
  DISTANCE_SENSOR = 15,
  NONE = MATRIX,
}

export const LED_EFFECT_NAMES: Record<number, string> = {
  [LEDEffect.MATRIX]: "Matrix (Software)",
  [LEDEffect.RAINBOW]: "Rainbow Wave",
  [LEDEffect.BREATHING]: "Breathing",
  [LEDEffect.STATIC_RAINBOW]: "Static Rainbow",
  [LEDEffect.SOLID]: "Solid Color",
  [LEDEffect.PLASMA]: "Plasma",
  [LEDEffect.FIRE]: "Fire",
  [LEDEffect.OCEAN]: "Ocean Waves",
  [LEDEffect.MATRIX_RAIN]: "Matrix Rain",
  [LEDEffect.SPARKLE]: "Sparkle",
  [LEDEffect.BREATHING_RAINBOW]: "Breathing Rainbow",
  [LEDEffect.SPIRAL]: "Spiral",
  [LEDEffect.COLOR_CYCLE]: "Color Cycle",
  [LEDEffect.REACTIVE]: "Reactive",
  [LEDEffect.THIRD_PARTY]: "Third-Party Live",
  [LEDEffect.DISTANCE_SENSOR]: "Sensor Distance",
};

export const GAMEPAD_AXES = {
  None: 0,
  "Left Stick X": 1,
  "Left Stick Y": 2,
  "Right Stick X": 3,
  "Right Stick Y": 4,
  "Trigger L": 5,
  "Trigger R": 6,
} as const;

export const GAMEPAD_DIRECTIONS = {
  "+": 0,
  "-": 1,
} as const;

export const GAMEPAD_BUTTONS = {
  None: 0,
  A: 1,
  B: 2,
  X: 3,
  Y: 4,
  LB: 5,
  RB: 6,
  "LT (Full Trigger)": 7,
  "RT (Full Trigger)": 8,
  "Back / Minus": 9,
  "Start / Plus": 10,
  L3: 11,
  R3: 12,
  "DPad Up": 13,
  "DPad Down": 14,
  "DPad Left": 15,
  "DPad Right": 16,
  "Guide / Home": 17,
} as const;

export const GAMEPAD_KEYBOARD_ROUTING = {
  Disabled: 0,
  "All Keys": 1,
  "Unmapped Only": 2,
} as const;

export const GAMEPAD_API_MODES = {
  "HID (DirectInput)": 0,
  "XInput (Xbox Compatible)": 1,
} as const;

export const KEY_BEHAVIORS = {
  Normal: 0,
  "Tap-Hold": 1,
  Toggle: 2,
  "Dynamic Mapping": 3,
} as const;

export const ROTARY_ACTIONS = {
  Volume: 0,
  "LED Brightness": 1,
  "Effect Speed": 2,
  "Effect Cycle": 3,
  "RGB Customizer": 4,
} as const;

export const ROTARY_BUTTON_ACTIONS = {
  "Play / Pause": 0,
  Mute: 1,
  "Toggle LEDs": 2,
  "Next Effect": 3,
  "Cycle Rotary Action": 4,
} as const;

export const ROTARY_RGB_BEHAVIORS = {
  Hue: 0,
  Brightness: 1,
  "Effect Speed": 2,
  "Effect Cycle": 3,
} as const;

export const ROTARY_PROGRESS_STYLES = {
  "Solid Color": 0,
  "Rainbow Bar": 1,
  "Effect Palette": 2,
} as const;

export const SOCD_RESOLUTIONS = {
  "Last Input Wins": 0,
  "Most Pressed Wins": 1,
} as const;

export const LAYER_NAMES: Record<number, string> = {
  0: "Base",
  1: "Fn",
  2: "Layer 2",
  3: "Layer 3",
};

export const HID_KEYCODES: Record<string, number> = {
  NO: 0x0000,
  TRANSPARENT: 0x0001,
  A: 0x04,
  B: 0x05,
  C: 0x06,
  D: 0x07,
  E: 0x08,
  F: 0x09,
  G: 0x0a,
  H: 0x0b,
  I: 0x0c,
  J: 0x0d,
  K: 0x0e,
  L: 0x0f,
  M: 0x10,
  N: 0x11,
  O: 0x12,
  P: 0x13,
  Q: 0x14,
  R: 0x15,
  S: 0x16,
  T: 0x17,
  U: 0x18,
  V: 0x19,
  W: 0x1a,
  X: 0x1b,
  Y: 0x1c,
  Z: 0x1d,
  1: 0x1e,
  2: 0x1f,
  3: 0x20,
  4: 0x21,
  5: 0x22,
  6: 0x23,
  7: 0x24,
  8: 0x25,
  9: 0x26,
  0: 0x27,
  ENTER: 0x28,
  ESC: 0x29,
  BACKSPACE: 0x2a,
  TAB: 0x2b,
  SPACE: 0x2c,
  MINUS: 0x2d,
  EQUAL: 0x2e,
  LEFTBRACE: 0x2f,
  RIGHTBRACE: 0x30,
  BACKSLASH: 0x31,
  NONUS_HASH: 0x32,
  SEMICOLON: 0x33,
  APOSTROPHE: 0x34,
  GRAVE: 0x35,
  COMMA: 0x36,
  DOT: 0x37,
  SLASH: 0x38,
  CAPSLOCK: 0x39,
  NUMLOCK: 0x53,
  SCROLLLOCK: 0x47,
  F1: 0x3a,
  F2: 0x3b,
  F3: 0x3c,
  F4: 0x3d,
  F5: 0x3e,
  F6: 0x3f,
  F7: 0x40,
  F8: 0x41,
  F9: 0x42,
  F10: 0x43,
  F11: 0x44,
  F12: 0x45,
  PRINTSCREEN: 0x46,
  PAUSE: 0x48,
  INSERT: 0x49,
  HOME: 0x4a,
  PAGEUP: 0x4b,
  DELETE: 0x4c,
  END: 0x4d,
  PAGEDOWN: 0x4e,
  RIGHT: 0x4f,
  LEFT: 0x50,
  DOWN: 0x51,
  UP: 0x52,
  KP_DIVIDE: 0x54,
  KP_MULTIPLY: 0x55,
  KP_MINUS: 0x56,
  KP_PLUS: 0x57,
  KP_ENTER: 0x58,
  KP_1: 0x59,
  KP_2: 0x5a,
  KP_3: 0x5b,
  KP_4: 0x5c,
  KP_5: 0x5d,
  KP_6: 0x5e,
  KP_7: 0x5f,
  KP_8: 0x60,
  KP_9: 0x61,
  KP_0: 0x62,
  KP_DOT: 0x63,
  NONUS_BACKSLASH: 0x64,
  APPLICATION: 0x65,
  KB_POWER: 0x66,
  KP_EQUAL: 0x67,
  F13: 0x68,
  F14: 0x69,
  F15: 0x6a,
  F16: 0x6b,
  F17: 0x6c,
  F18: 0x6d,
  F19: 0x6e,
  F20: 0x6f,
  F21: 0x70,
  F22: 0x71,
  F23: 0x72,
  F24: 0x73,
  EXECUTE: 0x74,
  HELP: 0x75,
  MENU: 0x76,
  SELECT: 0x77,
  STOP: 0x78,
  AGAIN: 0x79,
  UNDO: 0x7a,
  CUT: 0x7b,
  COPY: 0x7c,
  PASTE: 0x7d,
  FIND: 0x7e,
  LCTRL: 0xe0,
  LSHIFT: 0xe1,
  LALT: 0xe2,
  LGUI: 0xe3,
  RCTRL: 0xe4,
  RSHIFT: 0xe5,
  RALT: 0xe6,
  RGUI: 0xe7,
  MUTE: 0x7f,
  VOLUMEUP: 0x80,
  VOLUMEDOWN: 0x81,
  AUDIO_MUTE: 0x00a8,
  AUDIO_VOL_UP: 0x00a9,
  AUDIO_VOL_DOWN: 0x00aa,
  MEDIA_NEXT_TRACK: 0x00ab,
  MEDIA_PREV_TRACK: 0x00ac,
  MEDIA_STOP: 0x00ad,
  MEDIA_PLAY_PAUSE: 0x00ae,
  MEDIA_SELECT: 0x00af,
  MEDIA_EJECT: 0x00b0,
  MAIL: 0x00b1,
  CALCULATOR: 0x00b2,
  MY_COMPUTER: 0x00b3,
  WWW_SEARCH: 0x00b4,
  WWW_HOME: 0x00b5,
  WWW_BACK: 0x00b6,
  WWW_FORWARD: 0x00b7,
  WWW_STOP: 0x00b8,
  WWW_REFRESH: 0x00b9,
  WWW_FAVORITES: 0x00ba,
  MEDIA_FAST_FORWARD: 0x00bb,
  MEDIA_REWIND: 0x00bc,
  BRIGHTNESS_UP: 0x00bd,
  BRIGHTNESS_DOWN: 0x00be,
  CONTROL_PANEL: 0x00bf,
  "FN / MO Layer 1": 0xf000,
  "MO Layer 2": 0xf001,
  "MO Layer 3": 0xf002,
  "TG Layer 1": 0xf010,
  "TG Layer 2": 0xf011,
  "TG Layer 3": 0xf012,
  "Set Base Layer": 0xf018,
  "Set Fn Layer": 0xf019,
  "Set Layer 2": 0xf01a,
  "Set Layer 3": 0xf01b,
  "Clear Layer Toggles": 0xf01c,
  "LED Toggle": 0xf200,
  "LED Brightness Down": 0xf201,
  "LED Brightness Up": 0xf202,
  "LED Effect Prev": 0xf203,
  "LED Effect Next": 0xf204,
  "LED Speed Down": 0xf205,
  "LED Speed Up": 0xf206,
  "LED Color Next": 0xf207,
  "Gamepad Enable": 0xf300,
  "Gamepad Disable": 0xf301,
  "Gamepad Toggle": 0xf302,
  "GP A": 0xf320,
  "GP B": 0xf321,
  "GP X": 0xf322,
  "GP Y": 0xf323,
  "GP LB": 0xf324,
  "GP RB": 0xf325,
  "GP LT Trigger": 0xf326,
  "GP RT Trigger": 0xf327,
  "GP Back": 0xf328,
  "GP Start": 0xf329,
  "GP L3": 0xf32a,
  "GP R3": 0xf32b,
  "GP DPad Up": 0xf32c,
  "GP DPad Down": 0xf32d,
  "GP DPad Left": 0xf32e,
  "GP DPad Right": 0xf32f,
  "GP Home": 0xf330,
  "GP LS Right": 0xf340,
  "GP LS Left": 0xf341,
  "GP LS Down": 0xf342,
  "GP LS Up": 0xf343,
  "GP RS Right": 0xf344,
  "GP RS Left": 0xf345,
  "GP RS Down": 0xf346,
  "GP RS Up": 0xf347,
  MOUSE_LEFT: 0xf100,
  MOUSE_RIGHT: 0xf101,
  MOUSE_MIDDLE: 0xf102,
  MOUSE_BACK: 0xf103,
  MOUSE_FORWARD: 0xf104,
  MOUSE_WHEEL_UP: 0xf105,
  MOUSE_WHEEL_DOWN: 0xf106,
  MOUSE_WHEEL_LEFT: 0xf107,
  MOUSE_WHEEL_RIGHT: 0xf108,
  FN: 0xf000,
};

export const HID_KEYCODE_NAMES = Object.fromEntries(
  Object.entries(HID_KEYCODES).map(([name, value]) => [value, name]),
) as Record<number, string>;
HID_KEYCODE_NAMES[0xf000] = "FN / MO Layer 1";

export function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

export function u16le(bytes: ArrayLike<number>, offset: number): number {
  return (bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8);
}

export function i16le(bytes: ArrayLike<number>, offset: number): number {
  const value = u16le(bytes, offset);
  return value >= 0x8000 ? value - 0x10000 : value;
}

export function u32le(bytes: ArrayLike<number>, offset: number): number {
  return (
    (bytes[offset] ?? 0) |
    ((bytes[offset + 1] ?? 0) << 8) |
    ((bytes[offset + 2] ?? 0) << 16) |
    ((bytes[offset + 3] ?? 0) << 24)
  ) >>> 0;
}

export function pushU16(target: number[], value: number): void {
  const normalized = value & 0xffff;
  target.push(normalized & 0xff, (normalized >> 8) & 0xff);
}

export function pushI16(target: number[], value: number): void {
  pushU16(target, value < 0 ? 0x10000 + value : value);
}

export function buildCommandReport(command: number, data: ArrayLike<number> = []): Uint8Array {
  const report = new Uint8Array(REPORT_SIZE);
  report[0] = REPORT_ID;
  report[1] = command & 0xff;
  for (let index = 0; index < data.length && index + 2 < report.length; index += 1) {
    report[index + 2] = data[index] ?? 0;
  }
  return report;
}

export function formatFirmwareVersion(version: number): string {
  const major = (version >> 8) & 0xff;
  const minor = version & 0xff;
  return `${major}.${minor}`;
}

export function invertMap(record: Record<string, number>): Record<number, string> {
  return Object.fromEntries(Object.entries(record).map(([key, value]) => [value, key]));
}

export const GAMEPAD_AXIS_NAMES = invertMap(GAMEPAD_AXES);
export const GAMEPAD_DIRECTION_NAMES = invertMap(GAMEPAD_DIRECTIONS);
export const GAMEPAD_BUTTON_NAMES = invertMap(GAMEPAD_BUTTONS);
export const GAMEPAD_KEYBOARD_ROUTING_NAMES = invertMap(GAMEPAD_KEYBOARD_ROUTING);
export const GAMEPAD_API_MODE_NAMES = invertMap(GAMEPAD_API_MODES);
export const KEY_BEHAVIOR_NAMES = invertMap(KEY_BEHAVIORS);
export const ROTARY_ACTION_NAMES = invertMap(ROTARY_ACTIONS);
export const ROTARY_BUTTON_ACTION_NAMES = invertMap(ROTARY_BUTTON_ACTIONS);
export const ROTARY_RGB_BEHAVIOR_NAMES = invertMap(ROTARY_RGB_BEHAVIORS);
export const ROTARY_PROGRESS_STYLE_NAMES = invertMap(ROTARY_PROGRESS_STYLES);
export const SOCD_RESOLUTION_NAMES = invertMap(SOCD_RESOLUTIONS);
