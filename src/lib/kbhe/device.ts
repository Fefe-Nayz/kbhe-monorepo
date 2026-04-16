import {
  ADVANCED_TICK_RATE_DEFAULT,
  ADVANCED_TICK_RATE_MAX,
  ADVANCED_TICK_RATE_MIN,
  CALIBRATION_VALUES_PER_CHUNK,
  Command,
  DEVICE_SERIAL_MAX_LENGTH,
  formatFirmwareVersion,
  GAMEPAD_API_MODES,
  GAMEPAD_AXES,
  GAMEPAD_BUTTONS,
  GAMEPAD_CURVE_MAX_DISTANCE_MM,
  GAMEPAD_CURVE_POINT_COUNT,
  GAMEPAD_DIRECTIONS,
  GAMEPAD_KEYBOARD_ROUTING,
  i16le,
  KEY_BEHAVIORS,
  KEYBOARD_NAME_LENGTH,
  KEY_COUNT,
  KEY_SETTINGS_PER_CHUNK,
  KEY_STATES_PER_CHUNK,
  LED_BYTES_PER_CHUNK,
  LED_EFFECT_PARAM_COUNT,
  LED_EFFECT_PARAM_COLOR_R,
  LED_EFFECT_PARAM_COLOR_G,
  LED_EFFECT_PARAM_COLOR_B,
  LED_EFFECT_PARAM_SPEED,
  LED_AUDIO_SPECTRUM_BAND_COUNT,
  SETTINGS_PROFILE_NAME_LENGTH,
  LAYER_COUNT,
  Status,
  u16le,
  u32le,
  pushU16,
} from "./protocol";
import { invoke } from "@tauri-apps/api/core";
import { KbheCommander, kbheCommander } from "./commander";
import {
  kbheTransport,
  type KbheTransport,
  type KbheTransportConnectionState,
  type KbheTransportDeviceInfo,
} from "./transport";

export interface DynamicZone {
  end_mm_tenths: number;
  end_mm: number;
  hid_keycode: number;
}

export interface KeySettings {
  key_index: number;
  profile_index: number;
  layer_index: number;
  hid_keycode: number;
  actuation_point_mm: number;
  release_point_mm: number;
  rapid_trigger_press: number;
  rapid_trigger_release: number;
  socd_pair: number | null;
  socd_resolution: number;
  rapid_trigger_enabled: boolean;
  continuous_rapid_trigger: boolean;
  behavior_mode: number;
  hold_threshold_ms: number;
  secondary_hid_keycode: number;
  dynamic_zones: DynamicZone[];
  tap_hold_options: number;
  dks_bottom_out_point_mm: number;
  socd_fully_pressed_enabled: boolean;
  socd_fully_pressed_point_mm: number;
  disable_kb_on_gamepad: boolean;
}

export interface GamepadCurvePoint {
  x_01mm: number;
  x_mm: number;
  y: number;
}

export interface GamepadSettings {
  deadzone: number;
  keyboard_routing: number;
  square_mode: boolean;
  reactive_stick: boolean;
  api_mode: number;
  curve_points: GamepadCurvePoint[];
}

export interface RotaryBinding {
  mode: number;             // 0=internal, 1=keycode
  keycode: number;
  modifier_mask_exact: number;
  fallback_no_mod_keycode: number;
  layer_mode: number;       // 0=active, 1=fixed
  layer_index: number;
}

export interface RotaryEncoderSettings {
  rotation_action: number;
  button_action: number;
  sensitivity: number;
  step_size: number;
  invert_direction: boolean;
  rgb_behavior: number;
  rgb_effect_mode: number;
  progress_style: number;
  progress_effect_mode: number;
  progress_color: [number, number, number];
  cw_binding: RotaryBinding;
  ccw_binding: RotaryBinding;
  click_binding: RotaryBinding;
}

export interface RotaryState {
  button_pressed: boolean;
  last_direction: number;  // -1, 0, or 1
  step_counter: number;
}

export interface ProfileInfo {
  profile_index: number;
  profile_used_mask: number;
  name?: string;
}

export interface LedParamDesc {
  id: number;
  type: number;
  min: number;
  max: number;
  default_val: number;
  step: number;
}

export interface LedEffectSchema {
  effect_mode: number;
  descriptors: LedParamDesc[];
}

export interface CalibrationSettings {
  lut_zero_value: number;
  key_zero_values: number[];
  key_max_values: number[];
}

export interface GuidedCalibrationStatus {
  active: boolean;
  phase: number;
  current_key: number;
  progress_percent: number;
  sample_count?: number;
  phase_elapsed_ms?: number;
}

export interface KeyCurveSettings {
  key_index: number;
  curve_enabled: boolean;
  p1_x: number;
  p1_y: number;
  p2_x: number;
  p2_y: number;
}

export interface KeyGamepadMap {
  key_index: number;
  axis: number;
  direction: number;
  button: number;
}

export interface FilterParams {
  noise_band: number;
  alpha_min_denom: number;
  alpha_max_denom: number;
}

export interface AdcDebugValues {
  adc: number[];
  adc_raw: number[];
  adc_filtered: number[];
  scan_time_us: number;
  scan_rate_hz: number;
  task_times_us: Record<string, number> | null;
  analog_monitor_us: Record<string, number> | null;
  adc_payload_format: "legacy" | "extended";
}

export interface McuMetrics {
  temperature_c: number | null;
  temperature_valid: boolean;
  vref_mv: number;
  core_clock_hz: number;
  scan_cycle_us: number;
  scan_rate_hz: number;
  work_us: number;
  load_percent: number;
  load_permille: number;
}

export interface AdcChunk {
  start_index: number;
  values: number[];
}

export interface KeyStatesSnapshot {
  states: number[];
  distances: number[];
  distances_01mm: number[];
  distances_mm: number[];
}

interface NativeKeyStatesSnapshotWire {
  states?: number[];
  distances?: number[];
  distances_01mm?: number[];
  distances_mm?: number[];
  distances01mm?: number[];
  distancesMm?: number[];
}

export interface AdcCaptureStatus {
  active: boolean;
  key_index: number;
  duration_ms: number;
  sample_count: number;
  overflow_count: number;
}

export interface AdcCaptureReadback {
  active: boolean;
  key_index: number;
  total_samples: number;
  start_index: number;
  sample_count: number;
  raw_samples: number[];
  filtered_samples: number[];
}

export interface LockStates {
  num_lock: boolean;
  caps_lock: boolean;
  scroll_lock: boolean;
}

export interface DeviceIdentity {
  version: number;
  firmware_version: string;
  serial_number: string;
  keyboard_name: string;
}

type DevicePathLogger = ((message: string) => void) | undefined;

export class KBHEDevice {
  private keyStatesNativeCommandAvailable: boolean | null = null;

  constructor(
    private readonly commander: KbheCommander = kbheCommander,
    private readonly transport: KbheTransport = kbheTransport,
  ) {}

  async listDevices(): Promise<KbheTransportDeviceInfo[]> {
    return this.transport.listDevices();
  }

  async listRuntimeDevices(): Promise<KbheTransportDeviceInfo[]> {
    const devices = await this.listDevices();
    return devices.filter((device) => device.kind === "runtime");
  }

  async connect(path?: string, logger?: DevicePathLogger): Promise<boolean> {
    let targetPath = path;
    if (!targetPath) {
      if (logger) {
        logger("Searching for KBHE runtime device...");
      }
      const device = (await this.listRuntimeDevices())[0];
      if (!device) {
        throw new Error("Device not found");
      }
      targetPath = device.path;
      if (logger) {
        logger(`Found runtime Raw HID path: ${targetPath}`);
      }
    }
    await this.transport.connect(targetPath);
    return true;
  }

  async reconnect(logger?: DevicePathLogger): Promise<boolean> {
    const state = await this.transport.connectionState();
    const path = state.path;
    await this.disconnect();
    if (!path) {
      return this.connect(undefined, logger);
    }
    return this.connect(path, logger);
  }

  async disconnect(): Promise<void> {
    await this.transport.disconnect();
  }

  async connectionState(): Promise<KbheTransportConnectionState> {
    return this.transport.connectionState();
  }

  private async sendCommand(
    command: Command | number,
    data: ArrayLike<number> = [],
    timeoutMs = 100,
  ): Promise<Uint8Array | null> {
    return this.commander.sendCommand(command, data, timeoutMs);
  }

  private unpackU16(data: ArrayLike<number>, offset: number): number {
    return u16le(data, offset);
  }

  private decodeCString(data: ArrayLike<number>, offset: number, maxLength: number): string {
    const bytes = Array.from({ length: maxLength }, (_, index) => Number(data[offset + index] ?? 0) & 0xff);
    const nul = bytes.indexOf(0);
    const raw = nul >= 0 ? bytes.slice(0, nul) : bytes;
    return String.fromCharCode(...raw).trim();
  }

  private encodeKeyboardName(name: string): number[] {
    const chars = Array.from(name)
      .filter((char) => {
        const code = char.charCodeAt(0);
        return code >= 0x20 && code <= 0x7e;
      })
      .slice(0, KEYBOARD_NAME_LENGTH);
    const bytes = Array.from({ length: KEYBOARD_NAME_LENGTH }, () => 0);
    chars.forEach((char, index) => {
      bytes[index] = char.charCodeAt(0);
    });
    return bytes;
  }

  private chunkCount(totalCount: number, startIndex: number, chunkSize: number): number {
    const remaining = Math.max(0, Number(totalCount) - Number(startIndex));
    return Math.min(chunkSize, remaining);
  }

  private sanitizeSocdResolution(value: unknown): number {
    const normalized = Number(value);
    return normalized >= 0 && normalized <= 4 ? normalized : 0;
  }

  private sanitizeKeyBehaviorMode(value: unknown): number {
    const normalized = Number(value);
    return Object.values(KEY_BEHAVIORS).includes(normalized as never)
      ? normalized
      : KEY_BEHAVIORS.Normal;
  }

  private sanitizeAdvancedTickRate(value: unknown): number {
    const normalized = Number(value);
    if (!Number.isFinite(normalized)) {
      return ADVANCED_TICK_RATE_DEFAULT;
    }
    return Math.max(
      ADVANCED_TICK_RATE_MIN,
      Math.min(ADVANCED_TICK_RATE_MAX, Math.trunc(normalized)),
    );
  }

  private defaultDynamicZones(primaryKeycode = 0x14): DynamicZone[] {
    return [
      { end_mm_tenths: 40, end_mm: 4.0, hid_keycode: primaryKeycode },
      { end_mm_tenths: 40, end_mm: 4.0, hid_keycode: 0 },
      { end_mm_tenths: 40, end_mm: 4.0, hid_keycode: 0 },
      { end_mm_tenths: 40, end_mm: 4.0, hid_keycode: 0 },
    ];
  }

  private sanitizeDynamicZones(zones: unknown, primaryKeycode = 0x14): DynamicZone[] {
    const defaults = this.defaultDynamicZones(primaryKeycode);
    const source = Array.isArray(zones) ? zones : [];
    const sanitized: DynamicZone[] = [];
    let previousEnd = 0;

    for (let index = 0; index < 4; index += 1) {
      const zone = (source[index] ?? defaults[index]) as Partial<DynamicZone> & {
        end_mm?: number;
      };
      let endMmTenths =
        typeof zone.end_mm_tenths === "number"
          ? Math.trunc(zone.end_mm_tenths)
          : Math.round(Number(zone.end_mm ?? defaults[index].end_mm) * 10.0);
      const hidKeycode =
        typeof zone.hid_keycode === "number"
          ? Math.trunc(zone.hid_keycode)
          : defaults[index].hid_keycode;

      endMmTenths = Math.max(previousEnd || 1, Math.min(40, endMmTenths));
      previousEnd = endMmTenths;
      sanitized.push({
        end_mm_tenths: endMmTenths,
        end_mm: endMmTenths / 10.0,
        hid_keycode: hidKeycode,
      });
    }

    if (sanitized.every((zone) => zone.hid_keycode === 0)) {
      sanitized[0].hid_keycode = primaryKeycode;
    }

    return sanitized;
  }

  private defaultGamepadCurvePoints(): GamepadCurvePoint[] {
    return [
      { x_01mm: 0, x_mm: 0.0, y: 0 },
      { x_01mm: 133, x_mm: 1.33, y: 85 },
      { x_01mm: 266, x_mm: 2.66, y: 170 },
      { x_01mm: 400, x_mm: 4.0, y: 255 },
    ];
  }

  private sanitizeGamepadRouting(value: unknown): number {
    const normalized = Number(value);
    return Object.values(GAMEPAD_KEYBOARD_ROUTING).includes(normalized as never)
      ? normalized
      : GAMEPAD_KEYBOARD_ROUTING["All Keys"];
  }

  private sanitizeGamepadApiMode(value: unknown): number {
    const normalized = Number(value);
    return Object.values(GAMEPAD_API_MODES).includes(normalized as never)
      ? normalized
      : GAMEPAD_API_MODES["HID (DirectInput)"];
  }

  private sanitizeGamepadCurvePoints(points: unknown): GamepadCurvePoint[] {
    const defaults = this.defaultGamepadCurvePoints();
    const source = Array.isArray(points) ? points : [];
    const sanitized: GamepadCurvePoint[] = [];
    let previousX = 0;

    for (let index = 0; index < GAMEPAD_CURVE_POINT_COUNT; index += 1) {
      const point = (source[index] ?? defaults[index]) as Partial<GamepadCurvePoint>;
      let x01mm =
        typeof point.x_01mm === "number"
          ? Math.trunc(point.x_01mm)
          : Math.round(Number(point.x_mm ?? defaults[index].x_mm) * 100.0);
      let y =
        typeof point.y === "number" ? Math.trunc(point.y) : defaults[index].y;

      x01mm = Math.max(previousX, Math.min(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100, x01mm));
      y = Math.max(0, Math.min(255, y));
      previousX = x01mm;
      sanitized.push({ x_01mm: x01mm, x_mm: x01mm / 100.0, y });
    }

    return sanitized;
  }

  private gamepadCurveStartDeadzone(points: GamepadCurvePoint[]): number {
    if (points.length === 0) {
      return 0;
    }
    const start01mm = Math.max(
      0,
      Math.min(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100, Math.trunc(points[0].x_01mm)),
    );
    return Math.round((start01mm * 255.0) / (GAMEPAD_CURVE_MAX_DISTANCE_MM * 100.0));
  }

  async getFirmwareVersion(): Promise<string | null> {
    const response = await this.sendCommand(Command.GET_FIRMWARE_VERSION);
    if (response && response.length >= 4) {
      const version = response[2] | (response[3] << 8);
      const major = (version >> 8) & 0xff;
      const minor = version & 0xff;
      return `${major}.${minor}`;
    }
    return null;
  }

  async getDeviceInfo(): Promise<DeviceIdentity | null> {
    const response = await this.sendCommand(Command.GET_DEVICE_INFO);
    if (response && response.length >= 62 && response[1] === Status.OK) {
      const version = this.unpackU16(response, 2);
      return {
        version,
        firmware_version: formatFirmwareVersion(version),
        serial_number: this.decodeCString(response, 4, DEVICE_SERIAL_MAX_LENGTH),
        keyboard_name: this.decodeCString(
          response,
          4 + DEVICE_SERIAL_MAX_LENGTH,
          KEYBOARD_NAME_LENGTH,
        ),
      };
    }
    return null;
  }

  async getKeyboardName(): Promise<string | null> {
    const response = await this.sendCommand(Command.GET_KEYBOARD_NAME);
    if (response && response.length >= 34 && response[1] === Status.OK) {
      return this.decodeCString(response, 2, KEYBOARD_NAME_LENGTH);
    }
    return null;
  }

  async setKeyboardName(name: string): Promise<string | null> {
    const payload = [0, ...this.encodeKeyboardName(name)];
    const response = await this.sendCommand(Command.SET_KEYBOARD_NAME, payload, 500);
    if (response && response.length >= 34 && response[1] === Status.OK) {
      return this.decodeCString(response, 2, KEYBOARD_NAME_LENGTH);
    }
    return null;
  }

  async reboot(): Promise<boolean> {
    const response = await this.sendCommand(Command.REBOOT, [], 500);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async enterBootloader(): Promise<boolean> {
    const response = await this.sendCommand(Command.ENTER_BOOTLOADER, [], 500);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getOptions(): Promise<{
    keyboard_enabled: boolean;
    gamepad_enabled: boolean;
    raw_hid_echo: boolean;
  } | null> {
    const response = await this.sendCommand(Command.GET_OPTIONS);
    if (response && response.length >= 5 && response[1] === Status.OK) {
      return {
        keyboard_enabled: Boolean(response[2]),
        gamepad_enabled: Boolean(response[3]),
        raw_hid_echo: Boolean(response[4]),
      };
    }
    return null;
  }

  async setKeyboardEnabled(enabled: boolean): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_KEYBOARD_ENABLED, [0, enabled ? 1 : 0], 3000);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async setGamepadEnabled(enabled: boolean): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_GAMEPAD_ENABLED, [0, enabled ? 1 : 0], 3000);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getNkroEnabled(): Promise<boolean | null> {
    const response = await this.sendCommand(Command.GET_NKRO_ENABLED);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return Boolean(response[2]);
    }
    return null;
  }

  async setNkroEnabled(enabled: boolean): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_NKRO_ENABLED, [0, enabled ? 1 : 0], 3000);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getAdvancedTickRate(): Promise<number | null> {
    const response = await this.sendCommand(Command.GET_ADVANCED_TICK_RATE);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return this.sanitizeAdvancedTickRate(response[2]);
    }
    return null;
  }

  async setAdvancedTickRate(tickRate: number): Promise<boolean> {
    const value = this.sanitizeAdvancedTickRate(tickRate);
    const response = await this.sendCommand(Command.SET_ADVANCED_TICK_RATE, [0, value], 300);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async saveSettings(): Promise<boolean> {
    const response = await this.sendCommand(Command.SAVE_SETTINGS, [], 3000);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async factoryReset(): Promise<boolean> {
    const response = await this.sendCommand(Command.FACTORY_RESET, [], 3000);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async usbReenumerate(timeoutS = 6.0, logger?: DevicePathLogger): Promise<boolean> {
    const response = await this.sendCommand(Command.USB_REENUMERATE, [], 500);
    if (!response || response.length < 2 || response[1] !== Status.OK) {
      return false;
    }

    await this.disconnect();
    await new Promise((resolve) => window.setTimeout(resolve, 350));

    const deadline = Date.now() + Math.max(1_000, timeoutS * 1000);
    let lastError: unknown = null;

    while (Date.now() < deadline) {
      try {
        await this.connect(undefined, logger);
        return true;
      } catch (error) {
        lastError = error;
        await new Promise((resolve) => window.setTimeout(resolve, 250));
      }
    }

    if (logger && lastError instanceof Error) {
      logger(`USB re-enumeration reconnect failed: ${lastError.message}`);
    }
    return false;
  }

  async ledGetEnabled(): Promise<boolean | null> {
    const response = await this.sendCommand(Command.GET_LED_ENABLED);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return Boolean(response[2]);
    }
    return null;
  }

  async ledSetEnabled(enabled: boolean): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_ENABLED, [0, enabled ? 1 : 0]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledGetBrightness(): Promise<number | null> {
    const response = await this.sendCommand(Command.GET_LED_BRIGHTNESS);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return response[2];
    }
    return null;
  }

  async ledSetBrightness(brightness: number): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_BRIGHTNESS, [
      0,
      Math.max(0, Math.min(255, Math.trunc(brightness))),
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledSetPixel(index: number, r: number, g: number, b: number): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_PIXEL, [0, index, r, g, b]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledGetPixel(index: number): Promise<[number, number, number] | null> {
    const response = await this.sendCommand(Command.GET_LED_PIXEL, [0, index]);
    if (response && response.length >= 6 && response[1] === Status.OK) {
      return [response[3], response[4], response[5]];
    }
    return null;
  }

  async ledGetRow(row: number): Promise<number[] | null> {
    const response = await this.sendCommand(Command.GET_LED_ROW, [0, row]);
    if (response && response.length >= 27 && response[1] === Status.OK) {
      return Array.from(response.slice(3, 27));
    }
    return null;
  }

  async ledSetRow(row: number, pixels: ArrayLike<number>): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_ROW, [
      0,
      row,
      ...Array.from(pixels).slice(0, 24),
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledClear(): Promise<boolean> {
    const response = await this.sendCommand(Command.LED_CLEAR);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledFill(r: number, g: number, b: number): Promise<boolean> {
    const response = await this.sendCommand(Command.LED_FILL, [0, r, g, b]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledTestRainbow(): Promise<boolean> {
    const response = await this.sendCommand(Command.LED_TEST_RAINBOW);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledSetVolumeOverlay(level: number): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_VOLUME_OVERLAY, [
      0,
      Math.max(0, Math.min(255, Math.trunc(level))),
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledClearVolumeOverlay(): Promise<boolean> {
    const response = await this.sendCommand(Command.CLEAR_LED_VOLUME_OVERLAY);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async ledUploadAll(pixels: ArrayLike<number>): Promise<boolean> {
    const totalSize = KEY_COUNT * 3;
    const pixelBytes = Array.from(pixels, (value) => value & 0xff).slice(0, totalSize);
    while (pixelBytes.length < totalSize) {
      pixelBytes.push(0);
    }

    let chunkIndex = 0;
    for (let offset = 0; offset < totalSize; offset += LED_BYTES_PER_CHUNK) {
      const chunk = pixelBytes.slice(offset, offset + LED_BYTES_PER_CHUNK);
      const response = await this.sendCommand(
        Command.SET_LED_ALL_CHUNK,
        [0, chunkIndex, chunk.length, ...chunk],
        200,
      );
      if (!response || response.length < 4 || response[1] !== Status.OK) {
        return false;
      }
      chunkIndex += 1;
      await new Promise((resolve) => window.setTimeout(resolve, 10));
    }
    return true;
  }

  async ledDownloadAll(): Promise<number[] | null> {
    const totalSize = KEY_COUNT * 3;
    const pixels: number[] = [];
    let chunkIndex = 0;

    while (pixels.length < totalSize) {
      const response = await this.sendCommand(Command.GET_LED_ALL, [0, chunkIndex], 200);
      if (!response || response.length < 4 || response[1] !== Status.OK) {
        return null;
      }

      const returnedChunk = response[2];
      const chunkSize = response[3];
      if (returnedChunk !== chunkIndex || chunkSize <= 0) {
        return null;
      }

      const payloadEnd = 4 + chunkSize;
      if (payloadEnd > response.length) {
        return null;
      }

      pixels.push(...Array.from(response.slice(4, payloadEnd)));
      chunkIndex += 1;
    }

    return pixels.slice(0, totalSize);
  }

  async getKeySettings(keyIndex: number, profileIndex = 0, layerIndex = 0): Promise<KeySettings | null> {
    const response = await this.sendCommand(Command.GET_KEY_SETTINGS, [0, keyIndex, profileIndex, layerIndex]);
    if (response && response.length >= 16 && response[1] === Status.OK) {
      // New packet layout:
      // [0]=cmd, [1]=status, [2]=key_index, [3]=profile_index, [4]=layer_index,
      // [5-6]=hid_keycode (u16 LE), [7]=actuation, [8]=release, [9]=rt_press, [10]=rt_release,
      // [11]=socd_pair, [12]=socd_resolution, [13]=rapid_trigger_enabled,
      // [14]=disable_kb_on_gamepad, [15]=continuous_rapid_trigger, [16]=behavior_mode,
      // [17]=hold_threshold_10ms, [18-19]=secondary_hid_keycode,
      // [20]=dz0.end, [21-22]=dz0.keycode, [23]=dz1.end, [24-25]=dz1.keycode,
      // [26]=dz2.end, [27-28]=dz2.keycode, [29]=dz3.end, [30-31]=dz3.keycode,
      // [32]=tap_hold_options, [33]=dks_bottom_out_point, [34]=socd_fully_pressed_enabled,
      // [35]=socd_fully_pressed_point
      const primaryKeycode = this.unpackU16(response, 5);
      let dynamicZones = this.defaultDynamicZones(primaryKeycode);

      if (response.length >= 32) {
        dynamicZones = this.sanitizeDynamicZones(
          Array.from({ length: 4 }, (_, index) => ({
            end_mm_tenths: response[20 + index * 3] ?? 0,
            hid_keycode: this.unpackU16(response, 21 + index * 3),
          })),
          primaryKeycode,
        );
      }

      return {
        key_index: response[2] ?? keyIndex,
        profile_index: response[3] ?? profileIndex,
        layer_index: response[4] ?? layerIndex,
        hid_keycode: primaryKeycode,
        actuation_point_mm: (response[7] ?? 20) / 10.0,
        release_point_mm: (response[8] ?? 18) / 10.0,
        rapid_trigger_press: (response[9] ?? 30) / 100.0,
        rapid_trigger_release: (response[10] ?? 30) / 100.0,
        socd_pair: (response[11] ?? 255) !== 255 ? (response[11] ?? 255) : null,
        socd_resolution: this.sanitizeSocdResolution(response[12] ?? 0),
        rapid_trigger_enabled: Boolean(response[13]),
        disable_kb_on_gamepad: Boolean(response[14]),
        continuous_rapid_trigger: Boolean(response[15]),
        behavior_mode: this.sanitizeKeyBehaviorMode(response[16] ?? KEY_BEHAVIORS.Normal),
        hold_threshold_ms: (response[17] ?? 20) * 10,
        secondary_hid_keycode: response.length >= 20 ? this.unpackU16(response, 18) : 0,
        dynamic_zones: dynamicZones,
        tap_hold_options: response[32] ?? 0,
        dks_bottom_out_point_mm: (response[33] ?? 40) / 10.0,
        socd_fully_pressed_enabled: Boolean(response[34]),
        socd_fully_pressed_point_mm: (response[35] ?? 40) / 10.0,
      };
    }
    return null;
  }

  async getLayerKeycode(
    layerIndex: number,
    keyIndex: number,
  ): Promise<{ layer_index: number; key_index: number; hid_keycode: number } | null> {
    const boundedLayerIndex = Math.max(0, Math.min(LAYER_COUNT - 1, Math.trunc(layerIndex)));
    const boundedKeyIndex = Math.max(0, Math.min(KEY_COUNT - 1, Math.trunc(keyIndex)));
    const response = await this.sendCommand(
      Command.GET_LAYER_KEYCODE,
      [0, boundedLayerIndex, boundedKeyIndex],
      150,
    );
    if (response && response.length >= 6 && response[1] === Status.OK) {
      return {
        layer_index: response[2],
        key_index: response[3],
        hid_keycode: this.unpackU16(response, 4),
      };
    }
    return null;
  }

  async setLayerKeycode(layerIndex: number, keyIndex: number, hidKeycode: number): Promise<boolean> {
    const boundedLayerIndex = Math.max(0, Math.min(LAYER_COUNT - 1, Math.trunc(layerIndex)));
    const boundedKeyIndex = Math.max(0, Math.min(KEY_COUNT - 1, Math.trunc(keyIndex)));
    const keycode = Math.trunc(hidKeycode) & 0xffff;
    const response = await this.sendCommand(
      Command.SET_LAYER_KEYCODE,
      [0, boundedLayerIndex, boundedKeyIndex, keycode & 0xff, (keycode >> 8) & 0xff],
      150,
    );
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async resetKeyTriggerSettings(keyIndex: number): Promise<boolean> {
    const boundedKeyIndex = Math.max(0, Math.min(KEY_COUNT - 1, Math.trunc(keyIndex)));
    const response = await this.sendCommand(
      Command.RESET_KEY_TRIGGER_SETTINGS,
      [0, boundedKeyIndex],
      300,
    );
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async setKeySettings(
    keyIndex: number,
    hidKeycode: number,
    actuationMm: number,
    releaseMm: number,
    rapidTriggerMm: number,
    socdPair: number | null = null,
    socdResolution = 0,
  ): Promise<boolean> {
    return this.setKeySettingsExtended(keyIndex, {
      hid_keycode: hidKeycode,
      actuation_point_mm: actuationMm,
      release_point_mm: releaseMm,
      rapid_trigger_enabled: false,
      rapid_trigger_press: rapidTriggerMm,
      rapid_trigger_release: rapidTriggerMm,
      socd_pair: socdPair ?? 255,
      socd_resolution: this.sanitizeSocdResolution(socdResolution),
      continuous_rapid_trigger: false,
      behavior_mode: KEY_BEHAVIORS.Normal,
      hold_threshold_ms: 200,
      secondary_hid_keycode: 0,
      dynamic_zones: this.defaultDynamicZones(hidKeycode),
    } as Partial<KeySettings>);
  }

  async setKeySettingsExtended(keyIndex: number, settings: Partial<KeySettings>): Promise<boolean> {
    const hidKeycode = Number(settings.hid_keycode ?? 0x14);
    const dynamicZones = this.sanitizeDynamicZones(settings.dynamic_zones, hidKeycode);
    const secondaryKeycode = Number(settings.secondary_hid_keycode ?? 0);
    const profileIndex = Math.trunc(settings.profile_index ?? 0) & 0xff;
    const layerIndex = Math.trunc(settings.layer_index ?? 0) & 0xff;

    const payload: number[] = [
      0,
      Math.trunc(keyIndex) & 0xff,
      profileIndex,
      layerIndex,
    ];
    pushU16(payload, hidKeycode);
    payload.push(
      Math.max(1, Math.min(255, Math.round((settings.actuation_point_mm ?? 2.0) * 10))),
      Math.max(0, Math.min(255, Math.round((settings.release_point_mm ?? 1.8) * 10))),
      Math.max(0, Math.min(255, Math.round((settings.rapid_trigger_press ?? 0.3) * 100))),
      Math.max(0, Math.min(255, Math.round((settings.rapid_trigger_release ?? 0.3) * 100))),
      Math.trunc(settings.socd_pair ?? 255) & 0xff,
      this.sanitizeSocdResolution(settings.socd_resolution ?? 0),
      settings.rapid_trigger_enabled ? 1 : 0,
      settings.disable_kb_on_gamepad ? 1 : 0,
      settings.continuous_rapid_trigger ? 1 : 0,
      this.sanitizeKeyBehaviorMode(settings.behavior_mode ?? KEY_BEHAVIORS.Normal),
      Math.max(1, Math.min(255, Math.round((settings.hold_threshold_ms ?? 200) / 10.0))),
    );
    pushU16(payload, secondaryKeycode);

    for (const zone of dynamicZones) {
      payload.push(zone.end_mm_tenths & 0xff);
      pushU16(payload, zone.hid_keycode);
    }

    payload.push(
      Math.trunc(settings.tap_hold_options ?? 0) & 0xff,
      Math.max(1, Math.min(255, Math.round((settings.dks_bottom_out_point_mm ?? 4.0) * 10))),
      settings.socd_fully_pressed_enabled ? 1 : 0,
      Math.max(1, Math.min(255, Math.round((settings.socd_fully_pressed_point_mm ?? 4.0) * 10))),
    );

    const response = await this.sendCommand(Command.SET_KEY_SETTINGS, payload);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getAllKeySettings(): Promise<KeySettings[] | null> {
    const keys: KeySettings[] = [];
    let nextIndex = 0;

    while (nextIndex < KEY_COUNT) {
      const response = await this.sendCommand(Command.GET_ALL_KEY_SETTINGS, [0, nextIndex], 150);
      if (!response || response.length < 4 || response[1] !== Status.OK) {
        return null;
      }

      const startIndex = response[2];
      const keyCount = response[3];
      if (startIndex !== nextIndex || keyCount <= 0 || keyCount > KEY_SETTINGS_PER_CHUNK) {
        return null;
      }

      const firstNewFlags = response[11];
      const firstLegacyFlags = response[12];
      const usesLegacyChunkLayout = !(firstNewFlags !== undefined && firstNewFlags <= 0x0f)
        && firstLegacyFlags !== undefined
        && firstLegacyFlags <= 0x0f;
      const entrySize = usesLegacyChunkLayout ? 9 : 8;

      for (let index = 0; index < keyCount; index += 1) {
        const offset = 4 + index * entrySize;
        const rapidPressIndex = usesLegacyChunkLayout ? 5 : 4;
        const rapidReleaseIndex = usesLegacyChunkLayout ? 6 : 5;
        const socdPairIndex = usesLegacyChunkLayout ? 7 : 6;
        const flagsIndex = usesLegacyChunkLayout ? 8 : 7;
        const hidKeycode = this.unpackU16(response, offset);
        const flags = response[offset + flagsIndex] ?? 0;
        keys.push({
          key_index: startIndex + index,
          profile_index: 0,
          layer_index: 0,
          hid_keycode: hidKeycode,
          actuation_point_mm: response[offset + 2] / 10.0,
          release_point_mm: response[offset + 3] / 10.0,
          rapid_trigger_press: response[offset + rapidPressIndex] / 100.0,
          rapid_trigger_release: response[offset + rapidReleaseIndex] / 100.0,
          socd_pair:
            response[offset + socdPairIndex] !== 255
              ? response[offset + socdPairIndex]
              : null,
          socd_resolution: this.sanitizeSocdResolution((flags >> 2) & 0x03),
          rapid_trigger_enabled: Boolean(flags & 0x01),
          continuous_rapid_trigger: false,
          behavior_mode: KEY_BEHAVIORS.Normal,
          hold_threshold_ms: 200,
          secondary_hid_keycode: 0,
          dynamic_zones: this.defaultDynamicZones(hidKeycode),
          tap_hold_options: 0,
          dks_bottom_out_point_mm: 4.0,
          socd_fully_pressed_enabled: false,
          socd_fully_pressed_point_mm: 4.0,
          disable_kb_on_gamepad: false,
        });
      }

      nextIndex += keyCount;
    }

    return keys;
  }

  async getGamepadSettings(): Promise<GamepadSettings | null> {
    const response = await this.sendCommand(Command.GET_GAMEPAD_SETTINGS);
    if (response && response.length >= 14 && response[1] === Status.OK) {
      // New layout (radial_deadzone removed):
      // [2]=keyboard_routing, [3]=square_mode, [4]=reactive_stick, [5]=api_mode,
      // [6+]=curve points (3 bytes each: x01mm lo, x01mm hi, y)
      const routing = this.sanitizeGamepadRouting(response[2]);
      const squareMode = Boolean(response[3]);
      const reactiveStick = Boolean(response[4]);
      const apiMode = this.sanitizeGamepadApiMode(response[5]);
      let offset = 6;

      const points: GamepadCurvePoint[] = [];
      for (let index = 0; index < GAMEPAD_CURVE_POINT_COUNT; index += 1) {
        const x01mm = this.unpackU16(response, offset);
        const y = response[offset + 2] ?? 0;
        points.push({ x_01mm: x01mm, x_mm: x01mm / 100.0, y });
        offset += 3;
      }
      const deadzone = this.gamepadCurveStartDeadzone(points);
      return {
        deadzone,
        keyboard_routing: routing,
        square_mode: squareMode,
        reactive_stick: reactiveStick,
        api_mode: apiMode,
        curve_points: this.sanitizeGamepadCurvePoints(points),
      };
    }
    return null;
  }

  async setGamepadSettings(settings: Partial<GamepadSettings>): Promise<boolean> {
    const routing = this.sanitizeGamepadRouting(
      settings.keyboard_routing ?? GAMEPAD_KEYBOARD_ROUTING["All Keys"],
    );
    const apiMode = this.sanitizeGamepadApiMode(
      settings.api_mode ?? GAMEPAD_API_MODES["HID (DirectInput)"],
    );
    const points = this.sanitizeGamepadCurvePoints(settings.curve_points);

    const payload = [
      0,
      routing,
      settings.square_mode ? 1 : 0,
      settings.reactive_stick ? 1 : 0,
      apiMode,
    ];
    for (const point of points) {
      payload.push(point.x_01mm & 0xff, (point.x_01mm >> 8) & 0xff, point.y & 0xff);
    }

    const response = await this.sendCommand(Command.SET_GAMEPAD_SETTINGS, payload);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  private defaultRotaryBinding(): RotaryBinding {
    return { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 };
  }

  private parseRotaryBinding(response: Uint8Array, offset: number): RotaryBinding {
    return {
      mode: response[offset] ?? 0,
      keycode: u16le(response, offset + 1),
      modifier_mask_exact: response[offset + 3] ?? 0,
      fallback_no_mod_keycode: u16le(response, offset + 4),
      layer_mode: response[offset + 6] ?? 0,
      layer_index: response[offset + 7] ?? 0,
    };
  }

  private pushRotaryBinding(payload: number[], binding: RotaryBinding): void {
    payload.push(binding.mode & 0xff);
    pushU16(payload, binding.keycode);
    payload.push(binding.modifier_mask_exact & 0xff);
    pushU16(payload, binding.fallback_no_mod_keycode);
    payload.push(binding.layer_mode & 0xff, binding.layer_index & 0xff);
  }

  async getRotaryEncoderSettings(): Promise<RotaryEncoderSettings | null> {
    const response = await this.sendCommand(Command.GET_ROTARY_ENCODER_SETTINGS);
    if (response && response.length >= 14 && response[1] === Status.OK) {
      const base = {
        rotation_action: response[2] ?? 0,
        button_action: response[3] ?? 0,
        sensitivity: response[4] ?? 1,
        step_size: response[5] ?? 1,
        invert_direction: Boolean(response[6]),
        rgb_behavior: response[7] ?? 0,
        rgb_effect_mode: response[8] ?? 0,
        progress_style: response[9] ?? 0,
        progress_effect_mode: response[10] ?? 0,
        progress_color: [response[11] ?? 0, response[12] ?? 0, response[13] ?? 0] as [number, number, number],
        cw_binding: this.defaultRotaryBinding(),
        ccw_binding: this.defaultRotaryBinding(),
        click_binding: this.defaultRotaryBinding(),
      };

      // Parse bindings if response is long enough (14 base + 8 bytes each = 38 total)
      if (response.length >= 38) {
        base.cw_binding = this.parseRotaryBinding(response, 14);
        base.ccw_binding = this.parseRotaryBinding(response, 22);
        base.click_binding = this.parseRotaryBinding(response, 30);
      }

      return base;
    }
    return null;
  }

  async setRotaryEncoderSettings(settings: Partial<RotaryEncoderSettings>): Promise<boolean> {
    const progressColor = [...(settings.progress_color ?? [40, 210, 64])] as number[];
    while (progressColor.length < 3) {
      progressColor.push(0);
    }
    const payload: number[] = [
      0,
      Math.trunc(settings.rotation_action ?? 0) & 0xff,
      Math.trunc(settings.button_action ?? 0) & 0xff,
      Math.trunc(settings.sensitivity ?? 1) & 0xff,
      Math.trunc(settings.step_size ?? 1) & 0xff,
      settings.invert_direction ? 1 : 0,
      Math.trunc(settings.rgb_behavior ?? 0) & 0xff,
      Math.trunc(settings.rgb_effect_mode ?? 4) & 0xff,
      Math.trunc(settings.progress_style ?? 0) & 0xff,
      Math.trunc(settings.progress_effect_mode ?? 1) & 0xff,
      progressColor[0] & 0xff,
      progressColor[1] & 0xff,
      progressColor[2] & 0xff,
    ];

    const cwBinding = settings.cw_binding ?? this.defaultRotaryBinding();
    const ccwBinding = settings.ccw_binding ?? this.defaultRotaryBinding();
    const clickBinding = settings.click_binding ?? this.defaultRotaryBinding();
    this.pushRotaryBinding(payload, cwBinding);
    this.pushRotaryBinding(payload, ccwBinding);
    this.pushRotaryBinding(payload, clickBinding);

    const response = await this.sendCommand(Command.SET_ROTARY_ENCODER_SETTINGS, payload);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getCalibration(): Promise<CalibrationSettings | null> {
    const keyZeros = Array.from({ length: KEY_COUNT }, () => 0);
    const keyMaxs = Array.from({ length: KEY_COUNT }, () => 0);
    let lutZero: number | null = null;
    let nextIndex = 0;

    while (nextIndex < KEY_COUNT) {
      const response = await this.sendCommand(Command.GET_CALIBRATION, [0, nextIndex], 150);
      if (!response || response.length < 6 || response[1] !== Status.OK) {
        return null;
      }
      const startIndex = response[2];
      const valueCount = response[3];
      if (
        startIndex !== nextIndex ||
        valueCount <= 0 ||
        valueCount > CALIBRATION_VALUES_PER_CHUNK
      ) {
        return null;
      }

      const currentLutZero = i16le(response, 4);
      if (lutZero === null) {
        lutZero = currentLutZero;
      }

      for (let index = 0; index < valueCount; index += 1) {
        const base = 6 + index * 2;
        keyZeros[startIndex + index] = i16le(response, base);
      }

      nextIndex += valueCount;
    }

    nextIndex = 0;
    while (nextIndex < KEY_COUNT) {
      const response = await this.sendCommand(Command.GET_CALIBRATION_MAX, [0, nextIndex], 150);
      if (!response || response.length < 6 || response[1] !== Status.OK) {
        return null;
      }
      const startIndex = response[2];
      const valueCount = response[3];
      if (
        startIndex !== nextIndex ||
        valueCount <= 0 ||
        valueCount > CALIBRATION_VALUES_PER_CHUNK
      ) {
        return null;
      }

      for (let index = 0; index < valueCount; index += 1) {
        const base = 6 + index * 2;
        keyMaxs[startIndex + index] = i16le(response, base);
      }

      nextIndex += valueCount;
    }

    return {
      lut_zero_value: lutZero ?? 0,
      key_zero_values: keyZeros,
      key_max_values: keyMaxs,
    };
  }

  async setCalibration(
    lutZero: number,
    keyZeros: ArrayLike<number>,
    keyMaxs: ArrayLike<number> = [],
  ): Promise<boolean> {
    const zeros = Array.from(keyZeros, (value) => Math.trunc(value));
    while (zeros.length < KEY_COUNT) {
      zeros.push(Math.trunc(lutZero));
    }
    zeros.length = KEY_COUNT;

    const maxs = Array.from(keyMaxs, (value) => Math.trunc(value));
    while (maxs.length < KEY_COUNT) {
      maxs.push(4095);
    }
    maxs.length = KEY_COUNT;

    let nextIndex = 0;
    while (nextIndex < KEY_COUNT) {
      const count = this.chunkCount(KEY_COUNT, nextIndex, CALIBRATION_VALUES_PER_CHUNK);
      const payload: number[] = [0, nextIndex, count];
      payload.push(Math.trunc(lutZero) & 0xff, (Math.trunc(lutZero) >> 8) & 0xff);
      for (const value of zeros.slice(nextIndex, nextIndex + count)) {
        payload.push(Math.trunc(value) & 0xff, (Math.trunc(value) >> 8) & 0xff);
      }
      const response = await this.sendCommand(Command.SET_CALIBRATION, payload, 300);
      if (!response || response.length < 4 || response[1] !== Status.OK) {
        return false;
      }
      nextIndex += count;
    }

    nextIndex = 0;
    while (nextIndex < KEY_COUNT) {
      const count = this.chunkCount(KEY_COUNT, nextIndex, CALIBRATION_VALUES_PER_CHUNK);
      const payload: number[] = [0, nextIndex, count];
      payload.push(Math.trunc(lutZero) & 0xff, (Math.trunc(lutZero) >> 8) & 0xff);
      for (const value of maxs.slice(nextIndex, nextIndex + count)) {
        payload.push(Math.trunc(value) & 0xff, (Math.trunc(value) >> 8) & 0xff);
      }
      const response = await this.sendCommand(Command.SET_CALIBRATION_MAX, payload, 300);
      if (!response || response.length < 4 || response[1] !== Status.OK) {
        return false;
      }
      nextIndex += count;
    }

    return true;
  }

  async autoCalibrate(keyIndex = 0xff): Promise<CalibrationSettings | null> {
    const response = await this.sendCommand(Command.AUTO_CALIBRATE, [0, keyIndex]);
    if (!response || response.length < 2 || response[1] !== Status.OK) {
      return null;
    }
    return this.getCalibration();
  }

  async guidedCalibrationStart(): Promise<GuidedCalibrationStatus | null> {
    const response = await this.sendCommand(Command.GUIDED_CALIBRATION_START, [], 500);
    if (!response || response.length < 10 || response[1] !== Status.OK) {
      return null;
    }
    return this.guidedCalibrationStatus();
  }

  async guidedCalibrationStatus(): Promise<GuidedCalibrationStatus | null> {
    const response = await this.sendCommand(Command.GUIDED_CALIBRATION_STATUS, [], 150);
    if (!response || response.length < 10 || response[1] !== Status.OK) {
      return null;
    }
    return {
      active: Boolean(response[2]),
      phase: response[3],
      current_key: response[4],
      progress_percent: response[5],
      sample_count: this.unpackU16(response, 6),
      phase_elapsed_ms: this.unpackU16(response, 8),
    };
  }

  async guidedCalibrationAbort(): Promise<GuidedCalibrationStatus | null> {
    const response = await this.sendCommand(Command.GUIDED_CALIBRATION_ABORT, [], 300);
    if (!response || response.length < 2 || response[1] !== Status.OK) {
      return null;
    }
    return {
      active: response.length >= 3 ? Boolean(response[2]) : false,
      phase: response.length >= 4 ? response[3] : 0,
      current_key: response.length >= 5 ? response[4] : 0,
      progress_percent: response.length >= 6 ? response[5] : 0,
    };
  }

  async getKeyCurve(keyIndex: number): Promise<KeyCurveSettings | null> {
    const response = await this.sendCommand(Command.GET_KEY_CURVE, [0, keyIndex]);
    if (response && response.length >= 8 && response[1] === Status.OK) {
      return {
        key_index: response[2],
        curve_enabled: response[3] !== 0,
        p1_x: response[4],
        p1_y: response[5],
        p2_x: response[6],
        p2_y: response[7],
      };
    }
    return null;
  }

  async setKeyCurve(
    keyIndex: number,
    curveEnabled: boolean,
    p1x: number,
    p1y: number,
    p2x: number,
    p2y: number,
  ): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_KEY_CURVE, [
      0,
      keyIndex,
      curveEnabled ? 1 : 0,
      p1x,
      p1y,
      p2x,
      p2y,
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getKeyGamepadMap(keyIndex: number): Promise<KeyGamepadMap | null> {
    const response = await this.sendCommand(Command.GET_KEY_GAMEPAD_MAP, [0, keyIndex]);
    if (response && response.length >= 6 && response[1] === Status.OK) {
      return {
        key_index: response[2],
        axis: response[3],
        direction: response[4],
        button: response[5],
      };
    }
    return null;
  }

  async setKeyGamepadMap(
    keyIndex: number,
    axis: string | number,
    direction: string | number,
    button: string | number,
  ): Promise<boolean> {
    const axisValue =
      typeof axis === "string" ? GAMEPAD_AXES[axis as keyof typeof GAMEPAD_AXES] ?? 0 : axis;
    const directionValue =
      typeof direction === "string"
        ? GAMEPAD_DIRECTIONS[direction as keyof typeof GAMEPAD_DIRECTIONS] ?? 0
        : direction;
    const buttonValue =
      typeof button === "string"
        ? GAMEPAD_BUTTONS[button as keyof typeof GAMEPAD_BUTTONS] ?? 0
        : button;
    const response = await this.sendCommand(Command.SET_KEY_GAMEPAD_MAP, [
      0,
      keyIndex,
      Number(axisValue) & 0xff,
      Number(directionValue) & 0xff,
      Number(buttonValue) & 0xff,
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getLedEffect(): Promise<number | null> {
    const response = await this.sendCommand(Command.GET_LED_EFFECT);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return response[2];
    }
    return null;
  }

  async setLedEffect(mode: number): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_EFFECT, [0, mode]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getLedEffectSpeed(): Promise<number | null> {
    const effect = await this.getLedEffect();
    if (effect == null) return null;
    const params = await this.getLedEffectParams(effect);
    if (!params) return null;
    return params[LED_EFFECT_PARAM_SPEED] ?? null;
  }

  async setLedEffectSpeed(speed: number): Promise<boolean> {
    const effect = await this.getLedEffect();
    if (effect == null) return false;
    const params = await this.getLedEffectParams(effect);
    if (!params) return false;
    const newParams = [...params];
    while (newParams.length <= LED_EFFECT_PARAM_SPEED) newParams.push(0);
    newParams[LED_EFFECT_PARAM_SPEED] = speed & 0xff;
    return this.setLedEffectParams(effect, newParams).then((r) => r !== null && r);
  }

  async setLedEffectColor(r: number, g: number, b: number): Promise<boolean> {
    const effect = await this.getLedEffect();
    if (effect == null) return false;
    const params = await this.getLedEffectParams(effect);
    if (!params) return false;
    const newParams = [...params];
    while (newParams.length <= LED_EFFECT_PARAM_COLOR_B) newParams.push(0);
    newParams[LED_EFFECT_PARAM_COLOR_R] = r & 0xff;
    newParams[LED_EFFECT_PARAM_COLOR_G] = g & 0xff;
    newParams[LED_EFFECT_PARAM_COLOR_B] = b & 0xff;
    return this.setLedEffectParams(effect, newParams).then((ok) => ok !== null && ok);
  }

  async getLedEffectColor(): Promise<[number, number, number] | null> {
    const effect = await this.getLedEffect();
    if (effect == null) return null;
    const params = await this.getLedEffectParams(effect);
    if (!params) return null;
    return [
      params[LED_EFFECT_PARAM_COLOR_R] ?? 255,
      params[LED_EFFECT_PARAM_COLOR_G] ?? 0,
      params[LED_EFFECT_PARAM_COLOR_B] ?? 0,
    ];
  }

  async getLedEffectParams(effectMode: number): Promise<number[] | null> {
    const response = await this.sendCommand(Command.GET_LED_EFFECT_PARAMS, [0, effectMode & 0xff]);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      const count = Math.min(response[3] ?? LED_EFFECT_PARAM_COUNT, LED_EFFECT_PARAM_COUNT, Math.max(0, response.length - 4));
      const params = Array.from(response.slice(4, 4 + count));
      while (params.length < LED_EFFECT_PARAM_COUNT) params.push(0);
      return params;
    }
    return null;
  }

  async setLedEffectParams(effectMode: number, params: ArrayLike<number>): Promise<boolean> {
    const values = Array.from(params, (value) => value & 0xff).slice(0, LED_EFFECT_PARAM_COUNT);
    while (values.length < LED_EFFECT_PARAM_COUNT) {
      values.push(0);
    }
    const response = await this.sendCommand(Command.SET_LED_EFFECT_PARAMS, [
      0,
      effectMode & 0xff,
      ...values,
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getLedFpsLimit(): Promise<number | null> {
    const response = await this.sendCommand(Command.GET_LED_FPS_LIMIT);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return response[2];
    }
    return null;
  }

  async setLedFpsLimit(fps: number): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_LED_FPS_LIMIT, [0, fps]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async restoreLedEffectBeforeThirdParty(): Promise<boolean> {
    const response = await this.sendCommand(Command.RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getLedEffectSchema(effectMode: number): Promise<LedEffectSchema | null> {
    const allDescriptors: LedParamDesc[] = [];
    let chunkIndex = 0;
    let totalChunks = 1;

    while (chunkIndex < totalChunks) {
      const response = await this.sendCommand(Command.GET_LED_EFFECT_SCHEMA, [0, effectMode & 0xff, chunkIndex]);
      if (!response || response.length < 6 || response[1] !== Status.OK) {
        return null;
      }
      const returnedEffect = response[2];
      if (returnedEffect !== effectMode) return null;
      totalChunks = response[4] ?? 1;
      const totalActive = response[5] ?? 0;
      const descriptorBytes = response.slice(6);
      const descriptorCount = Math.floor(descriptorBytes.length / 6);

      for (let i = 0; i < descriptorCount && allDescriptors.length < totalActive; i++) {
        const base = i * 6;
        allDescriptors.push({
          id: descriptorBytes[base] ?? 0,
          type: descriptorBytes[base + 1] ?? 0,
          min: descriptorBytes[base + 2] ?? 0,
          max: descriptorBytes[base + 3] ?? 255,
          default_val: descriptorBytes[base + 4] ?? 0,
          step: descriptorBytes[base + 5] ?? 1,
        });
      }

      chunkIndex += 1;
    }

    return { effect_mode: effectMode, descriptors: allDescriptors };
  }

  async setAudioSpectrum(bands: number[], impactLevel: number): Promise<boolean> {
    const bandCount = Math.min(bands.length, LED_AUDIO_SPECTRUM_BAND_COUNT);
    const bandBytes = bands.slice(0, bandCount).map((b) => b & 0xff);
    while (bandBytes.length < LED_AUDIO_SPECTRUM_BAND_COUNT) bandBytes.push(0);
    const response = await this.sendCommand(Command.SET_LED_AUDIO_SPECTRUM, [
      0,
      bandCount & 0xff,
      ...bandBytes,
      impactLevel & 0xff,
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async clearAudioSpectrum(): Promise<boolean> {
    const response = await this.sendCommand(Command.CLEAR_LED_AUDIO_SPECTRUM);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getRotaryState(): Promise<RotaryState | null> {
    const response = await this.sendCommand(Command.GET_ROTARY_STATE);
    if (response && response.length >= 8 && response[1] === Status.OK) {
      const lastDir = response[3] ?? 0;
      return {
        button_pressed: Boolean(response[2]),
        last_direction: lastDir > 127 ? lastDir - 256 : lastDir,
        step_counter: u32le(response, 4),
      };
    }
    return null;
  }

  private encodeProfileName(name: string): number[] {
    const chars = Array.from(name)
      .filter((char) => {
        const code = char.charCodeAt(0);
        return code >= 0x20 && code <= 0x7e;
      })
      .slice(0, SETTINGS_PROFILE_NAME_LENGTH);
    const bytes = Array.from({ length: SETTINGS_PROFILE_NAME_LENGTH }, () => 0);
    chars.forEach((char, index) => {
      bytes[index] = char.charCodeAt(0);
    });
    return bytes;
  }

  async getActiveProfile(): Promise<ProfileInfo | null> {
    const response = await this.sendCommand(Command.GET_ACTIVE_PROFILE);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      return {
        profile_index: response[2] ?? 0,
        profile_used_mask: response[3] ?? 0,
      };
    }
    return null;
  }

  async setActiveProfile(profileIndex: number): Promise<ProfileInfo | null> {
    const response = await this.sendCommand(Command.SET_ACTIVE_PROFILE, [0, profileIndex & 0xff], 500);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      return {
        profile_index: response[2] ?? 0,
        profile_used_mask: response[3] ?? 0,
      };
    }
    return null;
  }

  async getProfileName(profileIndex: number): Promise<{ name: string; profile_used_mask: number } | null> {
    const response = await this.sendCommand(Command.GET_PROFILE_NAME, [0, profileIndex & 0xff]);
    if (response && response.length >= 20 && response[1] === Status.OK) {
      return {
        profile_used_mask: response[3] ?? 0,
        name: this.decodeCString(response, 4, SETTINGS_PROFILE_NAME_LENGTH),
      };
    }
    return null;
  }

  async setProfileName(profileIndex: number, name: string): Promise<{ name: string } | null> {
    const payload = [0, profileIndex & 0xff, ...this.encodeProfileName(name)];
    const response = await this.sendCommand(Command.SET_PROFILE_NAME, payload, 500);
    if (response && response.length >= 20 && response[1] === Status.OK) {
      return { name: this.decodeCString(response, 4, SETTINGS_PROFILE_NAME_LENGTH) };
    }
    return null;
  }

  async createProfile(name?: string): Promise<ProfileInfo | null> {
    const payload: number[] = [0, ...(name ? this.encodeProfileName(name) : new Array(SETTINGS_PROFILE_NAME_LENGTH).fill(0))];
    const response = await this.sendCommand(Command.CREATE_PROFILE, payload, 500);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      return {
        profile_index: response[2] ?? 0,
        profile_used_mask: response[3] ?? 0,
        name: name,
      };
    }
    return null;
  }

  async deleteProfile(profileIndex: number): Promise<ProfileInfo | null> {
    const response = await this.sendCommand(Command.DELETE_PROFILE, [0, profileIndex & 0xff], 500);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      return {
        profile_index: response[2] ?? 0,
        profile_used_mask: response[3] ?? 0,
      };
    }
    return null;
  }

  async copyProfileSlot(sourceIndex: number, targetIndex: number): Promise<ProfileInfo | null> {
    const response = await this.sendCommand(Command.COPY_PROFILE_SLOT, [0, sourceIndex & 0xff, targetIndex & 0xff], 1000);
    if (response && response.length >= 5 && response[1] === Status.OK) {
      return {
        profile_index: response[3] ?? targetIndex,
        profile_used_mask: response[4] ?? 0,
      };
    }
    return null;
  }

  async resetProfileSlot(profileIndex: number): Promise<ProfileInfo | null> {
    const response = await this.sendCommand(Command.RESET_PROFILE_SLOT, [0, profileIndex & 0xff], 500);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      return {
        profile_index: response[2] ?? 0,
        profile_used_mask: response[3] ?? 0,
      };
    }
    return null;
  }

  async getFilterEnabled(): Promise<boolean | null> {
    const response = await this.sendCommand(Command.GET_FILTER_ENABLED);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      return response[2] !== 0;
    }
    return null;
  }

  async setFilterEnabled(enabled: boolean): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_FILTER_ENABLED, [0, enabled ? 1 : 0]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getFilterParams(): Promise<FilterParams | null> {
    const response = await this.sendCommand(Command.GET_FILTER_PARAMS);
    if (response && response.length >= 5 && response[1] === Status.OK) {
      return {
        noise_band: response[2],
        alpha_min_denom: response[3],
        alpha_max_denom: response[4],
      };
    }
    return null;
  }

  async setFilterParams(
    noiseBand: number,
    alphaMinDenom: number,
    alphaMaxDenom: number,
  ): Promise<boolean> {
    const response = await this.sendCommand(Command.SET_FILTER_PARAMS, [
      0,
      noiseBand,
      alphaMinDenom,
      alphaMaxDenom,
    ]);
    return !!response && response.length >= 2 && response[1] === Status.OK;
  }

  async getAdcValues(): Promise<AdcDebugValues | null> {
    const response = await this.sendCommand(Command.GET_ADC_VALUES);
    if (!response || response.length < 18 || response[1] !== Status.OK) {
      return null;
    }

    const legacyValues = Array.from({ length: 6 }, (_, index) => u16le(response, 2 + index * 2));
    const legacyScanTimeUs = u16le(response, 14);
    const legacyScanRateHz = u16le(response, 16);

    if (response.length >= 30) {
      const rawValues = Array.from({ length: 6 }, (_, index) => u16le(response, 2 + index * 2));
      const filteredValues = Array.from({ length: 6 }, (_, index) => u16le(response, 14 + index * 2));
      const scanTimeUs = u16le(response, 26);
      const scanRateHz = u16le(response, 28);

      let taskTimesUs: Record<string, number> | null = null;
      if (response.length >= 46) {
        taskTimesUs = {
          analog: u16le(response, 30),
          trigger: u16le(response, 32),
          socd: u16le(response, 34),
          keyboard: u16le(response, 36),
          keyboard_nkro: u16le(response, 38),
          gamepad: u16le(response, 40),
          led: u16le(response, 42),
          total: u16le(response, 44),
        };
      }

      let analogMonitorUs: Record<string, number> | null = null;
      if (response.length >= 64) {
        analogMonitorUs = {
          raw: u16le(response, 46),
          filter: u16le(response, 48),
          calibration: u16le(response, 50),
          lut: u16le(response, 52),
          store: u16le(response, 54),
          key_min: u16le(response, 56),
          key_max: u16le(response, 58),
          key_avg: u16le(response, 60),
          nonzero_keys: u16le(response, 62),
        };
      }

      if (
        scanTimeUs === 0 &&
        scanRateHz === 0 &&
        (legacyScanTimeUs !== 0 || legacyScanRateHz !== 0)
      ) {
        return {
          adc: legacyValues,
          adc_raw: legacyValues,
          adc_filtered: legacyValues,
          scan_time_us: legacyScanTimeUs,
          scan_rate_hz: legacyScanRateHz,
          task_times_us: null,
          analog_monitor_us: null,
          adc_payload_format: "legacy",
        };
      }

      return {
        adc: rawValues,
        adc_raw: rawValues,
        adc_filtered: filteredValues,
        scan_time_us: scanTimeUs,
        scan_rate_hz: scanRateHz,
        task_times_us: taskTimesUs,
        analog_monitor_us: analogMonitorUs,
        adc_payload_format: "extended",
      };
    }

    return {
      adc: legacyValues,
      adc_raw: legacyValues,
      adc_filtered: legacyValues,
      scan_time_us: legacyScanTimeUs,
      scan_rate_hz: legacyScanRateHz,
      task_times_us: null,
      analog_monitor_us: null,
      adc_payload_format: "legacy",
    };
  }

  async getMcuMetrics(): Promise<McuMetrics | null> {
    const response = await this.sendCommand(Command.GET_MCU_METRICS, [], 120);
    if (!response || response.length < 19 || response[1] !== Status.OK) {
      return null;
    }

    const temperatureRaw = i16le(response, 2);
    const vrefMv = this.unpackU16(response, 4);
    const coreClockHz = u32le(response, 6);
    const scanCycleUs = this.unpackU16(response, 10);
    const scanRateHz = this.unpackU16(response, 12);
    const workUs = this.unpackU16(response, 14);
    const loadPermille = this.unpackU16(response, 16);
    const tempValid = Boolean(response[18]);

    return {
      temperature_c: tempValid ? temperatureRaw : null,
      temperature_valid: tempValid,
      vref_mv: vrefMv,
      core_clock_hz: coreClockHz,
      scan_cycle_us: scanCycleUs,
      scan_rate_hz: scanRateHz,
      work_us: workUs,
      load_percent: loadPermille / 10.0,
      load_permille: loadPermille,
    };
  }

  private async getAdcChunk(command: Command, startIndex: number): Promise<AdcChunk | null> {
    const boundedStartIndex = Math.max(0, Math.min(255, Math.trunc(startIndex)));
    const response = await this.sendCommand(command, [0, boundedStartIndex], 150);
    if (response && response.length >= 4 && response[1] === Status.OK) {
      const returnedStart = response[2];
      const valueCount = Math.min(response[3], Math.floor((response.length - 4) / 2));
      const values = Array.from({ length: valueCount }, (_, index) =>
        u16le(response, 4 + index * 2),
      );
      return { start_index: returnedStart, values };
    }
    return null;
  }

  async getRawAdcChunk(startIndex: number): Promise<AdcChunk | null> {
    return this.getAdcChunk(Command.GET_RAW_ADC_CHUNK, startIndex);
  }

  async getFilteredAdcChunk(startIndex: number): Promise<AdcChunk | null> {
    return this.getAdcChunk(Command.GET_FILTERED_ADC_CHUNK, startIndex);
  }

  async getCalibratedAdcChunk(startIndex: number): Promise<AdcChunk | null> {
    return this.getAdcChunk(Command.GET_CALIBRATED_ADC_CHUNK, startIndex);
  }

  private async getAllAdcValues(
    getter: (startIndex: number) => Promise<AdcChunk | null>,
    keyCount = KEY_COUNT,
  ): Promise<number[] | null> {
    const values = Array.from({ length: keyCount }, () => 0);
    let nextIndex = 0;

    while (nextIndex < keyCount) {
      const chunk = await getter(nextIndex);
      if (!chunk) {
        return null;
      }
      const startIndex = Math.trunc(chunk.start_index);
      const chunkValues = Array.from(chunk.values);
      if (startIndex >= keyCount || chunkValues.length === 0) {
        return null;
      }

      chunkValues.forEach((value, offset) => {
        const destination = startIndex + offset;
        if (destination < keyCount) {
          values[destination] = value;
        }
      });

      const advancedTo = startIndex + chunkValues.length;
      if (advancedTo <= nextIndex) {
        return null;
      }
      nextIndex = advancedTo;
    }

    return values;
  }

  async getAllRawAdcValues(keyCount = KEY_COUNT): Promise<number[] | null> {
    return this.getAllAdcValues((startIndex) => this.getRawAdcChunk(startIndex), keyCount);
  }

  async getAllFilteredAdcValues(keyCount = KEY_COUNT): Promise<number[] | null> {
    return this.getAllAdcValues((startIndex) => this.getFilteredAdcChunk(startIndex), keyCount);
  }

  async getAllCalibratedAdcValues(keyCount = KEY_COUNT): Promise<number[] | null> {
    return this.getAllAdcValues((startIndex) => this.getCalibratedAdcChunk(startIndex), keyCount);
  }

  private normalizeNativeKeyStatesSnapshot(
    snapshot: NativeKeyStatesSnapshotWire | null,
  ): KeyStatesSnapshot | null {
    if (!snapshot || !Array.isArray(snapshot.states) || !Array.isArray(snapshot.distances)) {
      return null;
    }

    const distances01mm = Array.isArray(snapshot.distances_01mm)
      ? snapshot.distances_01mm
      : Array.isArray(snapshot.distances01mm)
        ? snapshot.distances01mm
        : null;
    const distancesMm = Array.isArray(snapshot.distances_mm)
      ? snapshot.distances_mm
      : Array.isArray(snapshot.distancesMm)
        ? snapshot.distancesMm
        : null;

    if (!distances01mm || !distancesMm) {
      return null;
    }

    if (
      snapshot.states.length !== KEY_COUNT ||
      snapshot.distances.length !== KEY_COUNT ||
      distances01mm.length !== KEY_COUNT ||
      distancesMm.length !== KEY_COUNT
    ) {
      return null;
    }

    return {
      states: snapshot.states,
      distances: snapshot.distances,
      distances_01mm: distances01mm,
      distances_mm: distancesMm,
    };
  }

  async getKeyStates(): Promise<KeyStatesSnapshot | null> {
    if (this.keyStatesNativeCommandAvailable !== false) {
      try {
        const wireSnapshot = await invoke<NativeKeyStatesSnapshotWire>("kbhe_get_key_states");
        const snapshot = this.normalizeNativeKeyStatesSnapshot(wireSnapshot);
        if (snapshot) {
          this.keyStatesNativeCommandAvailable = true;
          return snapshot;
        }

        this.keyStatesNativeCommandAvailable = false;
      } catch {
        // Cache the miss so non-Tauri runtimes or old backends don't throw on every poll tick.
        this.keyStatesNativeCommandAvailable = false;
      }
    }

    const states = Array.from({ length: KEY_COUNT }, () => 0);
    const distances = Array.from({ length: KEY_COUNT }, () => 0);
    const distances01mm = Array.from({ length: KEY_COUNT }, () => 0);
    const distancesMm = Array.from({ length: KEY_COUNT }, () => 0);
    let nextIndex = 0;

    while (nextIndex < KEY_COUNT) {
      const response = await this.sendCommand(Command.GET_KEY_STATES, [0, nextIndex], 150);
      if (!response || response.length < 4 || response[1] !== Status.OK) {
        return null;
      }

      const startIndex = response[2];
      const keyCount = response[3];
      if (startIndex !== nextIndex || keyCount <= 0 || keyCount > KEY_STATES_PER_CHUNK) {
        return null;
      }

      for (let index = 0; index < keyCount; index += 1) {
        const offset = 4 + index * 4;
        const keyIndex = startIndex + index;
        states[keyIndex] = response[offset];
        distances[keyIndex] = response[offset + 1];
        const value01mm = u16le(response, offset + 2);
        distances01mm[keyIndex] = value01mm;
        distancesMm[keyIndex] = value01mm / 100.0;
      }

      nextIndex += keyCount;
    }

    return {
      states,
      distances,
      distances_01mm: distances01mm,
      distances_mm: distancesMm,
    };
  }

  async adcCaptureStart(keyIndex: number, durationMs: number): Promise<AdcCaptureStatus | null> {
    const boundedDuration = Math.max(1, Math.trunc(durationMs));
    const payload = [
      0,
      keyIndex & 0xff,
      0,
      boundedDuration & 0xff,
      (boundedDuration >> 8) & 0xff,
      (boundedDuration >> 16) & 0xff,
      (boundedDuration >> 24) & 0xff,
    ];
    const response = await this.sendCommand(Command.ADC_CAPTURE_START, payload);
    if (response && response.length >= 16 && response[1] === Status.OK) {
      return {
        active: Boolean(response[2]),
        key_index: response[3],
        duration_ms: u32le(response, 4),
        sample_count: u32le(response, 8),
        overflow_count: u32le(response, 12),
      };
    }
    return null;
  }

  async adcCaptureStatus(): Promise<AdcCaptureStatus | null> {
    const response = await this.sendCommand(Command.ADC_CAPTURE_STATUS);
    if (response && response.length >= 16 && response[1] === Status.OK) {
      return {
        active: Boolean(response[2]),
        key_index: response[3],
        duration_ms: u32le(response, 4),
        sample_count: u32le(response, 8),
        overflow_count: u32le(response, 12),
      };
    }
    return null;
  }

  async adcCaptureRead(
    startIndex: number,
    maxSamples = 12,
  ): Promise<AdcCaptureReadback | null> {
    const boundedSamples = Math.max(1, Math.min(12, Math.trunc(maxSamples)));
    const normalizedStartIndex = Math.trunc(startIndex) >>> 0;
    const payload = [
      0,
      normalizedStartIndex & 0xff,
      (normalizedStartIndex >> 8) & 0xff,
      (normalizedStartIndex >> 16) & 0xff,
      (normalizedStartIndex >> 24) & 0xff,
      boundedSamples & 0xff,
    ];
    const response = await this.sendCommand(Command.ADC_CAPTURE_READ, payload);
    if (!response || response.length < 13 || response[1] !== Status.OK) {
      return null;
    }

    const totalSamples = u32le(response, 4);
    const returnedStart = u32le(response, 8);
    const count = Math.min(response[12], 12);
    const rawSamples: number[] = [];
    const filteredSamples: number[] = [];
    const rawBase = 13;
    const filteredBase = rawBase + 24;

    for (let index = 0; index < count; index += 1) {
      rawSamples.push(u16le(response, rawBase + index * 2));
      filteredSamples.push(u16le(response, filteredBase + index * 2));
    }

    return {
      active: Boolean(response[2]),
      key_index: response[3],
      total_samples: totalSamples,
      start_index: returnedStart,
      sample_count: count,
      raw_samples: rawSamples,
      filtered_samples: filteredSamples,
    };
  }

  async getLockStates(): Promise<LockStates | null> {
    const response = await this.sendCommand(Command.GET_LOCK_STATES);
    if (response && response.length >= 3 && response[1] === Status.OK) {
      const lockByte = response[2];
      return {
        num_lock: Boolean(lockByte & 0x01),
        caps_lock: Boolean(lockByte & 0x02),
        scroll_lock: Boolean(lockByte & 0x04),
      };
    }
    return null;
  }
}

export const kbheDevice = new KBHEDevice();
