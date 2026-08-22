import { describe, expect, test } from "bun:test";
import {
  buildRgbGradientFrame,
  buildRgbRainbowFrame,
  isLibhmkRgbEffect,
  parseRgbBridgeDeviceList,
  parseRgbBridgeState,
  rgbBridgeSerialNumber,
  RGB_BRIDGE_LIBHMK_PID,
  RGB_BRIDGE_USAGE,
  RGB_BRIDGE_USAGE_PAGE,
  RGB_BRIDGE_VID,
  selectRgbBridgeDevice,
} from "./rgb-bridge";

const device = {
  path: "hid-path",
  vid: RGB_BRIDGE_VID,
  pid: RGB_BRIDGE_LIBHMK_PID,
  interfaceNumber: 2,
  usagePage: RGB_BRIDGE_USAGE_PAGE,
  usage: RGB_BRIDGE_USAGE,
  manufacturer: "KBHE",
  product: "KBHE 75HE (libhmk)",
  serialNumber: "RGB-DEVICE",
};

const state = {
  capabilities: {
    protocolMajor: 1,
    protocolMinor: 0,
    ledCount: 82,
    bytesPerPixel: 3,
    chunkBytes: 60,
    liveEffectId: 7,
    capabilities: 0x7f,
    colorOrder: 0,
  },
  enabled: true,
  brightness: 96,
  effect: 0,
};

describe("libhmk RGB bridge payload validation", () => {
  test("accepts only PID 0004 on the dedicated FFAB:00AB collection", () => {
    expect(parseRgbBridgeDeviceList([device])).toEqual([device]);
    expect(() => parseRgbBridgeDeviceList([{ ...device, pid: 0x0002 }])).toThrow(
      "incompatible HID identity",
    );
    expect(() => parseRgbBridgeDeviceList([{ ...device, usagePage: 0xff00 }])).toThrow(
      "incompatible HID identity",
    );
  });

  test("selects libhmk devices only by a unique stable serial", () => {
    const other = { ...device, path: "other-path", serialNumber: "RGB-OTHER" };
    expect(selectRgbBridgeDevice([device])).toBe(device);
    expect(selectRgbBridgeDevice([other, device], " RGB-DEVICE ")).toBe(device);
    expect(selectRgbBridgeDevice([other, device])).toBeNull();
    expect(() => selectRgbBridgeDevice([{ ...device, serialNumber: null }])).toThrow(
      "no stable USB serial number",
    );
    expect(() => selectRgbBridgeDevice([device, { ...device, path: "clone" }])).toThrow(
      "duplicate USB serial number",
    );
    expect(rgbBridgeSerialNumber(device)).toBe("RGB-DEVICE");
  });

  test("rejects incompatible capability descriptors before controls are enabled", () => {
    expect(parseRgbBridgeState(state)).toEqual(state);
    expect(() => parseRgbBridgeState({
      ...state,
      capabilities: { ...state.capabilities, protocolMajor: 2 },
    })).toThrow("unsupported protocol");
    expect(() => parseRgbBridgeState({
      ...state,
      capabilities: { ...state.capabilities, bytesPerPixel: 4 },
    })).toThrow("unsupported protocol");
    expect(() => parseRgbBridgeState({ ...state, enabled: 1 })).toThrow(
      "must be boolean or null",
    );
    expect(() => parseRgbBridgeState({ ...state, effect: 42 })).toThrow(
      "unsupported libhmk RGB effect",
    );
  });

  test("rejects malformed and impossible frame geometry", () => {
    expect(() => parseRgbBridgeState({
      ...state,
      capabilities: { ...state.capabilities, chunkBytes: 0 },
    })).toThrow("chunk bytes");
    expect(() => parseRgbBridgeState({
      ...state,
      capabilities: { ...state.capabilities, ledCount: 255, chunkBytes: 1 },
    })).toThrow("too many chunks");
  });

  test("keeps libhmk effect IDs on the explicit bridge allowlist", () => {
    expect([0, 1, 2, 3, 7].every(isLibhmkRgbEffect)).toBe(true);
    expect([4, 6, 8, 42, 255].some(isLibhmkRgbEffect)).toBe(false);
  });

  test("builds exact RGB live frames for gradient and rainbow presets", () => {
    expect(buildRgbGradientFrame(3, [255, 0, 0], [0, 0, 255])).toEqual([
      255, 0, 0,
      128, 0, 128,
      0, 0, 255,
    ]);
    const rainbow = buildRgbRainbowFrame(82);
    expect(rainbow).toHaveLength(82 * 3);
    expect(rainbow.every((component) => component >= 0 && component <= 255)).toBe(true);
    expect(() => buildRgbGradientFrame(0, [0, 0, 0], [0, 0, 0])).toThrow("LED count");
  });
});
