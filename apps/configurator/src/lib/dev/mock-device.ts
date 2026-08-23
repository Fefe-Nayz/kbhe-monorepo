/**
 * Development-only fake keyboard.
 *
 * The configurator talks to the firmware exclusively through Tauri `invoke`.
 * Running `vite dev` in a plain browser therefore leaves every page in its
 * error/empty state, which makes UI work on the populated layouts impossible.
 *
 * This module installs a minimal `__TAURI_INTERNALS__` shim that answers the
 * `kbhe_*` commands with plausible data. It is imported behind `import.meta.env.DEV`
 * and only activates when explicitly asked for, so it can never reach a build.
 *
 * Enable with `?mock=1` in the URL, or `localStorage.setItem("kbhe-mock-device","1")`.
 * Disable with `?mock=0`.
 */

import {
  Command,
  DEVICE_SERIAL_MAX_LENGTH,
  KEYBOARD_NAME_LENGTH,
  KEY_COUNT,
  LAYER_COUNT,
  PACKET_SIZE,
  SETTINGS_PROFILE_NAME_LENGTH,
  Status,
} from "@/lib/kbhe/protocol";

const STORAGE_KEY = "kbhe-mock-device";

export function isMockDeviceEnabled(): boolean {
  if (!import.meta.env.DEV) return false;
  try {
    const params = new URLSearchParams(window.location.search);
    const flag = params.get("mock");
    if (flag === "1" || flag === "true") {
      localStorage.setItem(STORAGE_KEY, "1");
      return true;
    }
    if (flag === "0" || flag === "false") {
      localStorage.removeItem(STORAGE_KEY);
      return false;
    }
    return localStorage.getItem(STORAGE_KEY) === "1";
  } catch {
    return false;
  }
}

const SERIAL = "KBHE-DEV-0000000000001";
const KEYBOARD_NAME = "KBHE 75HE (mock)";
const FIRMWARE = { major: 1, minor: 4, patch: 2 };

/** US ANSI HID usages roughly matching the 82-key default layout, for legible previews. */
const DEFAULT_KEYCODES: number[] = (() => {
  const rows = [
    [0x29, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x4c],
    [0x35, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x4a],
    [0x2b, 0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x31, 0x4b],
    [0x39, 0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x28, 0x4d],
    [0xe1, 0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x52, 0x4e],
    [0xe0, 0xe3, 0xe2, 0x2c, 0xe6, 0xe7, 0x65, 0xe4, 0x50, 0x51, 0x4f],
  ];
  const flat = rows.flat();
  return Array.from({ length: KEY_COUNT }, (_, i) => flat[i] ?? 0);
})();

interface MockState {
  keyboardEnabled: number;
  gamepadEnabled: number;
  nkroEnabled: number;
  ledEnabled: number;
  ledBrightness: number;
  ledEffect: number;
  ledFpsLimit: number;
  activeProfile: number;
  defaultProfile: number;
  profileUsedMask: number;
  ramOnlyMode: number;
  advancedTickRate: number;
  chatterGuard: number;
  filterEnabled: number;
  filterNoiseBand: number;
  filterAlphaMin: number;
  filterAlphaMax: number;
  lockBits: number;
  profileNames: string[];
  /** layer -> key -> keycode */
  keycodes: number[][];
  bootTime: number;
}

function createState(): MockState {
  return {
    keyboardEnabled: 1,
    gamepadEnabled: 0,
    nkroEnabled: 1,
    ledEnabled: 1,
    ledBrightness: 180,
    ledEffect: 5,
    ledFpsLimit: 120,
    activeProfile: 0,
    defaultProfile: 0,
    profileUsedMask: 0b0011,
    ramOnlyMode: 0,
    advancedTickRate: 8,
    chatterGuard: 3,
    filterEnabled: 1,
    filterNoiseBand: 30,
    filterAlphaMin: 32,
    filterAlphaMax: 4,
    lockBits: 0b001, // Num Lock on, like most desktops boot
    profileNames: ["Daily", "FPS", "", ""],
    keycodes: Array.from({ length: LAYER_COUNT }, (_, layer) =>
      DEFAULT_KEYCODES.map((code) => (layer === 0 ? code : 1 /* TRANSPARENT */)),
    ),
    bootTime: Date.now(),
  };
}

function writeCString(buf: number[], offset: number, value: string, maxLength: number) {
  const bytes = new TextEncoder().encode(value).slice(0, maxLength - 1);
  for (let i = 0; i < maxLength; i += 1) {
    buf[offset + i] = bytes[i] ?? 0;
  }
}

function writeU16(buf: number[], offset: number, value: number) {
  buf[offset] = value & 0xff;
  buf[offset + 1] = (value >> 8) & 0xff;
}

function writeU32(buf: number[], offset: number, value: number) {
  buf[offset] = value & 0xff;
  buf[offset + 1] = (value >> 8) & 0xff;
  buf[offset + 2] = (value >> 16) & 0xff;
  buf[offset + 3] = (value >> 24) & 0xff;
}

/** Stable per-key ADC baseline — real sensors never read identically. */
function keyBaseline(index: number): number {
  return 1850 + ((index * 137) % 420) - 210;
}

/** Slow drift so telemetry sparklines have something to draw. */
function wobble(base: number, amplitude: number, periodMs: number, phase = 0): number {
  const t = (Date.now() % periodMs) / periodMs;
  return base + Math.sin(t * Math.PI * 2 + phase) * amplitude;
}

export function installMockDevice(): void {
  const state = createState();

  const buildResponse = (command: number, data: number[]): number[] => {
    // Every real response echoes the request parameters that follow the leading
    // zero byte, then appends its payload — so start from that and override.
    const out = new Array<number>(PACKET_SIZE).fill(0);
    out[0] = command & 0xff;
    out[1] = Status.OK;
    for (let i = 1; i < data.length && i + 1 < PACKET_SIZE; i += 1) {
      out[i + 1] = data[i] ?? 0;
    }

    switch (command) {
      case Command.GET_FIRMWARE_VERSION: {
        out[2] = FIRMWARE.major;
        out[3] = FIRMWARE.minor;
        out[4] = FIRMWARE.patch;
        break;
      }
      case Command.GET_DEVICE_INFO: {
        out[2] = FIRMWARE.major;
        out[3] = FIRMWARE.minor;
        out[4] = FIRMWARE.patch;
        writeCString(out, 5, SERIAL, DEVICE_SERIAL_MAX_LENGTH);
        writeCString(out, 5 + DEVICE_SERIAL_MAX_LENGTH, KEYBOARD_NAME, KEYBOARD_NAME_LENGTH);
        break;
      }
      case Command.GET_KEYBOARD_NAME:
      case Command.SET_KEYBOARD_NAME: {
        writeCString(out, 2, KEYBOARD_NAME, KEYBOARD_NAME_LENGTH);
        break;
      }
      case Command.GET_KEYBOARD_ENABLED:
        out[2] = state.keyboardEnabled;
        break;
      case Command.SET_KEYBOARD_ENABLED:
        state.keyboardEnabled = data[1] ?? 0;
        out[2] = state.keyboardEnabled;
        break;
      case Command.GET_GAMEPAD_ENABLED:
        out[2] = state.gamepadEnabled;
        break;
      case Command.SET_GAMEPAD_ENABLED:
        state.gamepadEnabled = data[1] ?? 0;
        out[2] = state.gamepadEnabled;
        break;
      case Command.GET_NKRO_ENABLED:
        out[2] = state.nkroEnabled;
        break;
      case Command.SET_NKRO_ENABLED:
        state.nkroEnabled = data[1] ?? 0;
        out[2] = state.nkroEnabled;
        break;
      case Command.GET_OPTIONS: {
        out[2] = state.keyboardEnabled;
        out[3] = state.gamepadEnabled;
        out[4] = state.ledEnabled;
        out[5] = state.nkroEnabled;
        out[6] = 1;
        break;
      }
      case Command.GET_ADVANCED_TICK_RATE:
        out[2] = state.advancedTickRate;
        break;
      case Command.SET_ADVANCED_TICK_RATE:
        state.advancedTickRate = data[1] ?? 8;
        out[2] = state.advancedTickRate;
        break;
      case Command.GET_TRIGGER_CHATTER_GUARD:
        out[2] = state.chatterGuard;
        break;
      case Command.SET_TRIGGER_CHATTER_GUARD:
        state.chatterGuard = data[1] ?? 0;
        out[2] = state.chatterGuard;
        break;
      case Command.GET_LED_ENABLED:
        out[2] = state.ledEnabled;
        break;
      case Command.SET_LED_ENABLED:
        state.ledEnabled = data[1] ?? 0;
        out[2] = state.ledEnabled;
        break;
      case Command.GET_LED_BRIGHTNESS:
        out[2] = state.ledBrightness;
        break;
      case Command.SET_LED_BRIGHTNESS:
        state.ledBrightness = data[1] ?? 0;
        out[2] = state.ledBrightness;
        break;
      case Command.GET_LED_EFFECT:
        out[2] = state.ledEffect;
        break;
      case Command.SET_LED_EFFECT:
        state.ledEffect = data[1] ?? 0;
        out[2] = state.ledEffect;
        break;
      case Command.GET_LED_FPS_LIMIT:
      case Command.SET_LED_FPS_LIMIT: {
        if (command === Command.SET_LED_FPS_LIMIT) state.ledFpsLimit = data[1] ?? 0;
        out[2] = state.ledFpsLimit;
        break;
      }
      case Command.GET_ACTIVE_PROFILE:
        out[2] = state.activeProfile;
        out[3] = state.profileUsedMask;
        break;
      case Command.SET_ACTIVE_PROFILE:
        state.activeProfile = data[1] ?? 0;
        out[2] = state.activeProfile;
        out[3] = state.profileUsedMask;
        break;
      case Command.GET_DEFAULT_PROFILE:
      case Command.SET_DEFAULT_PROFILE: {
        if (command === Command.SET_DEFAULT_PROFILE) state.defaultProfile = data[1] ?? 0;
        out[2] = state.defaultProfile;
        out[3] = state.profileUsedMask;
        break;
      }
      case Command.GET_PROFILE_NAME:
      case Command.SET_PROFILE_NAME: {
        const index = data[1] ?? 0;
        if (command === Command.SET_PROFILE_NAME) {
          const raw = data.slice(2, 2 + SETTINGS_PROFILE_NAME_LENGTH);
          state.profileNames[index] = new TextDecoder()
            .decode(new Uint8Array(raw))
            .replace(/\0.*$/, "");
        }
        // [2]=index, [3]=used mask, [4..]=name
        out[2] = index;
        out[3] = state.profileUsedMask;
        writeCString(out, 4, state.profileNames[index] ?? "", SETTINGS_PROFILE_NAME_LENGTH);
        break;
      }
      case Command.GET_RAM_ONLY_MODE:
      case Command.SET_RAM_ONLY_MODE: {
        if (command === Command.SET_RAM_ONLY_MODE) state.ramOnlyMode = data[1] ?? 0;
        out[2] = state.ramOnlyMode;
        break;
      }
      case Command.GET_LAYER_KEYCODE: {
        const layer = data[1] ?? 0;
        const key = data[2] ?? 0;
        out[2] = layer;
        out[3] = key;
        writeU16(out, 4, state.keycodes[layer]?.[key] ?? 0);
        break;
      }
      case Command.SET_LAYER_KEYCODE: {
        const layer = data[1] ?? 0;
        const key = data[2] ?? 0;
        const code = (data[3] ?? 0) | ((data[4] ?? 0) << 8);
        if (state.keycodes[layer]) state.keycodes[layer][key] = code;
        out[2] = layer;
        out[3] = key;
        writeU16(out, 4, code);
        break;
      }
      case Command.GET_KEY_SETTINGS: {
        const key = data[1] ?? 0;
        const profile = data[2] ?? 0;
        const layer = data[3] ?? 0;
        out[2] = key;
        out[3] = profile;
        out[4] = layer;
        writeU16(out, 5, state.keycodes[layer]?.[key] ?? 0);
        out[7] = 50;   // actuation point
        out[8] = 30;   // release point
        out[9] = 20;   // rapid-trigger press sensitivity
        out[10] = 20;  // rapid-trigger release sensitivity
        out[11] = 255; // socd_pair: 255 means "unpaired"
        out[13] = 1;   // rapid trigger enabled
        out[16] = 0;   // behavior_mode: Normal
        break;
      }
      case Command.GET_CALIBRATION:
      case Command.GET_CALIBRATION_MAX: {
        // Chunked reads: keep the echoed chunk index and fill plausible ADC counts.
        for (let i = 4; i < PACKET_SIZE - 1; i += 2) {
          writeU16(out, i, command === Command.GET_CALIBRATION ? 320 : 2650);
        }
        break;
      }
      case Command.GET_MCU_METRICS: {
        writeU16(out, 2, Math.round(wobble(38, 2, 40_000)));           // temperature °C
        writeU16(out, 4, Math.round(wobble(3298, 6, 26_000, 1.2)));   // vref mV
        writeU32(out, 6, 170_000_000);
        writeU16(out, 10, Math.round(wobble(122, 6, 17_000, 2.1)));   // scan cycle µs
        writeU16(out, 12, Math.round(wobble(8130, 90, 23_000)));      // scan rate Hz
        writeU16(out, 14, Math.round(wobble(41, 5, 13_000, 0.7)));    // work µs
        writeU16(out, 16, Math.round(wobble(336, 40, 19_000, 2.6)));  // load ‰
        out[18] = 1;                                                  // temperature valid
        writeU16(out, 19, 168);                                       // max scan cycle
        writeU32(out, 21, 0);
        writeU32(out, 25, 4821);
        writeU32(out, 29, 12);
        writeU32(out, 33, 3);
        writeU32(out, 37, 1);
        writeU32(out, 41, 2);
        writeU32(out, 45, 0);
        writeU16(out, 49, 64);
        out[51] = 0;
        out[52] = 1;
        writeU16(out, 53, 40);
        out[55] = 1;
        writeU16(out, 56, 149);
        break;
      }
      case Command.GET_RAW_ADC_CHUNK:
      case Command.GET_FILTERED_ADC_CHUNK:
      case Command.GET_CALIBRATED_ADC_CHUNK: {
        const start = data[1] ?? 0;
        const capacity = Math.floor((PACKET_SIZE - 4) / 2);
        const count = Math.max(0, Math.min(capacity, KEY_COUNT - start));
        out[2] = start;
        out[3] = count;
        for (let i = 0; i < count; i += 1) {
          const key = start + i;
          const base = keyBaseline(key);
          const value = command === Command.GET_RAW_ADC_CHUNK
            // Raw carries a little noise; filtered and calibrated do not.
            ? Math.round(base + wobble(0, 9, 700 + key * 13, key))
            : command === Command.GET_FILTERED_ADC_CHUNK
              ? base
              : Math.round(base * 0.98);
          writeU16(out, 4 + i * 2, value);
        }
        break;
      }
      case Command.GET_ADC_VALUES: {
        for (let i = 0; i < 6; i += 1) {
          writeU16(out, 2 + i * 2, keyBaseline(i));       // raw sample window
          writeU16(out, 14 + i * 2, keyBaseline(i));      // filtered sample window
        }
        writeU16(out, 26, Math.round(wobble(122, 6, 17_000, 2.1))); // scan time µs
        writeU16(out, 28, Math.round(wobble(8130, 90, 23_000)));    // scan rate Hz
        const taskTimes = [11, 6, 2, 5, 4, 3, 9, 41];
        taskTimes.forEach((value, i) => writeU16(out, 30 + i * 2, value));
        const analogMonitor = [1850, 1849, 1812, 1810, 1808, 1642, 2270, 1851, 0];
        analogMonitor.forEach((value, i) => writeU16(out, 46 + i * 2, value));
        break;
      }
      case Command.GET_FILTER_ENABLED:
        out[2] = state.filterEnabled;
        break;
      case Command.SET_FILTER_ENABLED:
        state.filterEnabled = data[1] ?? 0;
        out[2] = state.filterEnabled;
        break;
      case Command.GET_FILTER_PARAMS:
        out[2] = state.filterNoiseBand;
        out[3] = state.filterAlphaMin;
        out[4] = state.filterAlphaMax;
        break;
      case Command.SET_FILTER_PARAMS:
        state.filterNoiseBand = data[1] ?? state.filterNoiseBand;
        state.filterAlphaMin = data[2] ?? state.filterAlphaMin;
        state.filterAlphaMax = data[3] ?? state.filterAlphaMax;
        out[2] = state.filterNoiseBand;
        out[3] = state.filterAlphaMin;
        out[4] = state.filterAlphaMax;
        break;
      case Command.GET_LOCK_STATES:
        out[2] = state.lockBits;
        break;
      case Command.GET_KEY_STATES: {
        // Idle keyboard: everything resting at its calibrated zero.
        break;
      }
      default:
        break;
    }

    return out;
  };

  const devices = [
    {
      path: "mock://kbhe/0",
      vid: 0x1209,
      pid: 0x4b48,
      kind: "runtime" as const,
      interfaceNumber: 1,
      usagePage: 0xff00,
      usage: 0x01,
      manufacturer: "KBHE",
      product: KEYBOARD_NAME,
      serialNumber: SERIAL,
    },
  ];

  let connected = false;

  const handlers: Record<string, (args: Record<string, unknown>) => unknown> = {
    kbhe_list_devices: () => devices,
    kbhe_connect: () => {
      connected = true;
      return { connected: true, path: devices[0].path, pid: devices[0].pid, kind: "runtime" };
    },
    kbhe_disconnect: () => {
      connected = false;
      return { connected: false, path: null, pid: null, kind: null };
    },
    kbhe_connection_state: () => ({
      connected,
      path: connected ? devices[0].path : null,
      pid: connected ? devices[0].pid : null,
      kind: connected ? "runtime" : null,
    }),
    kbhe_wait_for_device: () => devices[0],
    // Never resolves: the real command blocks until the device physically leaves.
    kbhe_wait_for_disconnect: () => new Promise<boolean>(() => {}),
    kbhe_detect_bootloader_presence: () => false,
    kbhe_flush_input: () => 0,
    kbhe_write_report: () => PACKET_SIZE,
    kbhe_read_report: () => [],
    kbhe_send_command: (args) => {
      const command = Number(args.command ?? args.cmd ?? 0);
      const raw = (args.data ?? []) as ArrayLike<number>;
      return buildResponse(command, Array.from(raw));
    },
    kbhe_get_key_states: () => ({ keys: [], sequence: 0 }),
    kbhe_get_os_key_variants: () => ({}),
    kbhe_get_system_volume: () => 42,
    kbhe_get_audio_bands: () => Array.from({ length: 16 }, (_, i) => Math.round(wobble(90, 70, 900 + i * 60))),
    kbhe_get_startup_preferences: () => ({ launchOnStartup: false, startupWindowMode: "normal" }),
    kbhe_set_startup_preferences: () => null,
    kbhe_check_app_update: () => ({ available: false }),
    kbhe_check_firmware_update: () => ({ available: false }),
    kbhe_list_rgb_bridge_devices: () => [],
    kbhe_frontend_ready: () => null,
  };

  const internals = {
    invoke: async (cmd: string, args?: Record<string, unknown>) => {
      const handler = handlers[cmd];
      if (!handler) {
        throw new Error(`mock device: command ${cmd} is not implemented`);
      }
      // A tick of latency keeps loading states observable during design work.
      await new Promise((resolve) => setTimeout(resolve, 4));
      return handler(args ?? {});
    },
    transformCallback: (callback: (payload: unknown) => void) => {
      const id = Math.floor(Math.random() * 1_000_000);
      (window as unknown as Record<string, unknown>)[`_${id}`] = callback;
      return id;
    },
    convertFileSrc: (path: string) => path,
    plugins: {},
  };

  Object.defineProperty(window, "__TAURI_INTERNALS__", {
    value: internals,
    writable: true,
    configurable: true,
  });
  Object.defineProperty(window, "isTauri", {
    value: true,
    writable: true,
    configurable: true,
  });

  console.info(
    "%c[kbhe] mock device active",
    "color:#22c55e;font-weight:600",
    "— UI is talking to a simulated keyboard. Append ?mock=0 to disable.",
  );
}
