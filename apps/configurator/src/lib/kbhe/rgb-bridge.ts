import { invoke } from "@tauri-apps/api/core";

export const RGB_BRIDGE_VID = 0x9172;
export const RGB_BRIDGE_LIBHMK_PID = 0x0004;
export const RGB_BRIDGE_USAGE_PAGE = 0xffab;
export const RGB_BRIDGE_USAGE = 0x00ab;
export const RGB_BRIDGE_PROTOCOL_MAJOR = 1;

export const RGBBridgeCapability = {
  ENABLED: 1 << 0,
  BRIGHTNESS: 1 << 1,
  PIXEL: 1 << 2,
  FRAME_CHUNKS: 1 << 3,
  FILL: 1 << 4,
  LIVE_MODE: 1 << 5,
  RESTORE_MODE: 1 << 6,
} as const;

export const LibhmkRgbEffect = {
  STATIC: 0,
  BREATHING: 1,
  RAINBOW: 2,
  RAINBOW_WAVE: 3,
  LIVE: 7,
} as const;

export type LibhmkRgbEffectId = typeof LibhmkRgbEffect[keyof typeof LibhmkRgbEffect];

export function isLibhmkRgbEffect(value: number): value is LibhmkRgbEffectId {
  return value === 0 || value === 1 || value === 2 || value === 3 || value === 7;
}

export interface RgbBridgeDeviceInfo {
  path: string;
  vid: number;
  pid: number;
  interfaceNumber: number | null;
  usagePage: number | null;
  usage: number | null;
  manufacturer: string | null;
  product: string | null;
  serialNumber: string | null;
}

export type RgbBridgeTarget = Pick<RgbBridgeDeviceInfo, "path" | "serialNumber">;

export function rgbBridgeSerialNumber(device: RgbBridgeTarget): string {
  const serialNumber = device.serialNumber?.trim();
  if (!serialNumber) {
    throw new Error(
      "the libhmk RGB device exposes no stable USB serial number; refusing an unverifiable target",
    );
  }
  return serialNumber;
}

/** Select by stable serial, never by HID enumeration order. */
export function selectRgbBridgeDevice(
  devices: readonly RgbBridgeDeviceInfo[],
  selectedSerialNumber?: string | null,
): RgbBridgeDeviceInfo | null {
  if (devices.length === 0) return null;

  const serials = devices.map(rgbBridgeSerialNumber);
  if (new Set(serials).size !== serials.length) {
    throw new Error("refusing ambiguous libhmk RGB devices with a duplicate USB serial number");
  }

  const selected = selectedSerialNumber?.trim() ?? "";
  if (selected) {
    const match = devices.find((device) => rgbBridgeSerialNumber(device) === selected);
    if (!match) {
      throw new Error(`libhmk RGB device serial ${selected} is no longer present`);
    }
    return match;
  }
  return devices.length === 1 ? devices[0]! : null;
}

function rgbBridgeTargetArgs(device: RgbBridgeTarget): {
  path: string;
  expectedSerialNumber: string;
} {
  return {
    path: device.path,
    expectedSerialNumber: rgbBridgeSerialNumber(device),
  };
}

export interface RgbBridgeCapabilities {
  protocolMajor: number;
  protocolMinor: number;
  ledCount: number;
  bytesPerPixel: number;
  chunkBytes: number;
  liveEffectId: number;
  capabilities: number;
  colorOrder: number;
}

export interface RgbBridgeState {
  capabilities: RgbBridgeCapabilities;
  enabled: boolean | null;
  brightness: number | null;
  effect: number;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function integer(value: unknown, label: string, min: number, max: number): number {
  if (!Number.isInteger(value) || (value as number) < min || (value as number) > max) {
    throw new Error(`${label} must be an integer between ${min} and ${max}`);
  }
  return value as number;
}

function nullableString(value: unknown, label: string): string | null {
  if (value === null) return null;
  if (typeof value !== "string") throw new Error(`${label} must be a string or null`);
  return value;
}

function nullableInteger(value: unknown, label: string): number | null {
  if (value === null) return null;
  return integer(value, label, 0, 0xffff);
}

function nullableBoolean(value: unknown, label: string): boolean | null {
  if (value === null) return null;
  if (typeof value !== "boolean") throw new Error(`${label} must be boolean or null`);
  return value;
}

export function parseRgbBridgeDeviceList(value: unknown): RgbBridgeDeviceInfo[] {
  if (!Array.isArray(value)) {
    throw new Error("RGB bridge discovery returned a non-array payload");
  }

  return value.map((item, index) => {
    if (!isRecord(item)) throw new Error(`RGB bridge device ${index} is malformed`);
    if (typeof item.path !== "string" || item.path.length === 0) {
      throw new Error(`RGB bridge device ${index} has an invalid path`);
    }
    const vid = integer(item.vid, "RGB bridge VID", 0, 0xffff);
    const pid = integer(item.pid, "RGB bridge PID", 0, 0xffff);
    const usagePage = nullableInteger(item.usagePage, "RGB bridge usage page");
    const usage = nullableInteger(item.usage, "RGB bridge usage");
    if (
      vid !== RGB_BRIDGE_VID
      || pid !== RGB_BRIDGE_LIBHMK_PID
      || usagePage !== RGB_BRIDGE_USAGE_PAGE
      || usage !== RGB_BRIDGE_USAGE
    ) {
      throw new Error("RGB bridge discovery returned an incompatible HID identity");
    }

    return {
      path: item.path,
      vid,
      pid,
      interfaceNumber: nullableInteger(item.interfaceNumber, "RGB bridge interface"),
      usagePage,
      usage,
      manufacturer: nullableString(item.manufacturer, "RGB bridge manufacturer"),
      product: nullableString(item.product, "RGB bridge product"),
      serialNumber: nullableString(item.serialNumber, "RGB bridge serial number"),
    };
  });
}

export function parseRgbBridgeState(value: unknown): RgbBridgeState {
  if (!isRecord(value) || !isRecord(value.capabilities)) {
    throw new Error("RGB bridge state is malformed");
  }
  const raw = value.capabilities;
  const capabilities: RgbBridgeCapabilities = {
    protocolMajor: integer(raw.protocolMajor, "RGB protocol major", 0, 0xff),
    protocolMinor: integer(raw.protocolMinor, "RGB protocol minor", 0, 0xff),
    ledCount: integer(raw.ledCount, "RGB LED count", 1, 0xff),
    bytesPerPixel: integer(raw.bytesPerPixel, "RGB bytes per pixel", 1, 0xff),
    chunkBytes: integer(raw.chunkBytes, "RGB chunk bytes", 1, 60),
    liveEffectId: integer(raw.liveEffectId, "RGB live effect", 0, 0xff),
    capabilities: integer(raw.capabilities, "RGB capability bitmap", 0, 0xffff),
    colorOrder: integer(raw.colorOrder, "RGB color order", 0, 0xff),
  };
  if (
    capabilities.protocolMajor !== RGB_BRIDGE_PROTOCOL_MAJOR
    || capabilities.bytesPerPixel !== 3
    || capabilities.liveEffectId !== LibhmkRgbEffect.LIVE
    || capabilities.colorOrder !== 0
  ) {
    throw new Error("RGB bridge returned an unsupported protocol or geometry");
  }
  const frameBytes = capabilities.ledCount * capabilities.bytesPerPixel;
  if (Math.ceil(frameBytes / capabilities.chunkBytes) > 256) {
    throw new Error("RGB bridge frame requires too many chunks");
  }

  const enabled = nullableBoolean(value.enabled, "RGB enabled state");
  const brightness = value.brightness === null
    ? null
    : integer(value.brightness, "RGB brightness", 0, 0xff);

  const effect = integer(value.effect, "RGB effect", 0, 0xff);
  if (!isLibhmkRgbEffect(effect)) {
    throw new Error(`unsupported libhmk RGB effect ${effect}`);
  }

  return {
    capabilities,
    enabled,
    brightness,
    effect,
  };
}

function byte(value: number, label: string): number {
  return integer(value, label, 0, 0xff);
}

export function buildRgbGradientFrame(
  ledCount: number,
  start: readonly [number, number, number],
  end: readonly [number, number, number],
): number[] {
  integer(ledCount, "LED count", 1, 0xff);
  start.forEach((value) => byte(value, "gradient start component"));
  end.forEach((value) => byte(value, "gradient end component"));
  const denominator = Math.max(ledCount - 1, 1);
  return Array.from({ length: ledCount * 3 }, (_, offset) => {
    const led = Math.floor(offset / 3);
    const channel = offset % 3;
    return Math.round(start[channel]! + (end[channel]! - start[channel]!) * led / denominator);
  });
}

export function buildRgbRainbowFrame(ledCount: number): number[] {
  integer(ledCount, "LED count", 1, 0xff);
  const frame: number[] = [];
  for (let led = 0; led < ledCount; led += 1) {
    const hue = led / ledCount * 6;
    const section = Math.floor(hue);
    const fraction = hue - section;
    const rising = Math.round(fraction * 255);
    const falling = 255 - rising;
    const color = [
      [255, rising, 0],
      [falling, 255, 0],
      [0, 255, rising],
      [0, falling, 255],
      [rising, 0, 255],
      [255, 0, falling],
    ][section % 6]!;
    frame.push(...color);
  }
  return frame;
}

export class LibhmkRgbBridge {
  async listDevices(): Promise<RgbBridgeDeviceInfo[]> {
    return parseRgbBridgeDeviceList(await invoke<unknown>("kbhe_list_rgb_bridge_devices"));
  }

  async getState(device: RgbBridgeTarget): Promise<RgbBridgeState> {
    return parseRgbBridgeState(await invoke<unknown>(
      "kbhe_rgb_bridge_get_state",
      rgbBridgeTargetArgs(device),
    ));
  }

  async setEnabled(device: RgbBridgeTarget, enabled: boolean): Promise<void> {
    await invoke("kbhe_rgb_bridge_set_enabled", { ...rgbBridgeTargetArgs(device), enabled });
  }

  async setBrightness(device: RgbBridgeTarget, brightness: number): Promise<void> {
    await invoke("kbhe_rgb_bridge_set_brightness", {
      ...rgbBridgeTargetArgs(device),
      brightness: byte(brightness, "brightness"),
    });
  }

  async fill(device: RgbBridgeTarget, r: number, g: number, b: number): Promise<void> {
    await invoke("kbhe_rgb_bridge_fill", {
      ...rgbBridgeTargetArgs(device),
      r: byte(r, "red"),
      g: byte(g, "green"),
      b: byte(b, "blue"),
    });
  }

  async clear(device: RgbBridgeTarget): Promise<void> {
    await invoke("kbhe_rgb_bridge_clear", rgbBridgeTargetArgs(device));
  }

  async restoreEffect(device: RgbBridgeTarget): Promise<void> {
    await invoke("kbhe_rgb_bridge_restore_effect", rgbBridgeTargetArgs(device));
  }

  async setEffect(device: RgbBridgeTarget, effect: number): Promise<void> {
    if (!isLibhmkRgbEffect(effect)) {
      throw new Error(`unsupported libhmk RGB effect ${effect}`);
    }
    await invoke("kbhe_rgb_bridge_set_effect", { ...rgbBridgeTargetArgs(device), effect });
  }

  async setPixel(
    device: RgbBridgeTarget,
    index: number,
    r: number,
    g: number,
    b: number,
  ): Promise<void> {
    await invoke("kbhe_rgb_bridge_set_pixel", {
      ...rgbBridgeTargetArgs(device),
      index: byte(index, "pixel index"),
      r: byte(r, "red"),
      g: byte(g, "green"),
      b: byte(b, "blue"),
    });
  }

  async writeFrame(device: RgbBridgeTarget, frame: ArrayLike<number>): Promise<void> {
    const bytes = Array.from(frame, (value) => byte(value, "frame component"));
    await invoke("kbhe_rgb_bridge_write_frame", {
      ...rgbBridgeTargetArgs(device),
      frame: bytes,
    });
  }
}

export const libhmkRgbBridge = new LibhmkRgbBridge();
