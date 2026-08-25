import { describe, expect, test } from "bun:test";
import {
  captureUpdaterMigrationBackup,
  getUpdaterMigrationBackup,
  hasCompleteProfileRecovery,
  isResetCalibration,
  normalizeCalibrationSettings,
  restoreUpdaterMigrationBackup,
  type CalibrationMigrationStore,
  type FirmwareProfileMigrationApi,
  type UpdaterMigrationDevice,
} from "./calibration-migration";
import {
  KEY_COUNT,
  LAYER_COUNT,
  LED_EFFECT_COUNT,
  LED_EFFECT_PARAM_COUNT,
  SETTINGS_PROFILE_COUNT,
} from "./protocol";
import {
  ACTION_OVERLAY_COUNT,
  ACTION_PROGRAM_COUNT,
  defaultActionOverlayBinding,
  defaultActionProgram,
} from "./action-program";
import type { CalibrationSettings } from "./device";
import type { FirmwareProfileSnapshot } from "./profile-sync";

function calibration(offset = 0): CalibrationSettings {
  return {
    lut_zero_value: 2195 + offset,
    key_zero_values: Array.from({ length: KEY_COUNT }, (_, index) => 2140 + index % 45 + offset),
    key_max_values: Array.from({ length: KEY_COUNT }, (_, index) => 2820 + index % 35 + offset),
  };
}

function profileSnapshot(profileIndex: number, serialNumber: string): FirmwareProfileSnapshot {
  const binding = {
    mode: 0, keycode: 0, modifier_mask_exact: 0,
    fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0,
  };
  return {
    schemaVersion: 2,
    capturedAt: 1,
    sourceProfileIndex: profileIndex,
    profileId: `profile-${profileIndex}`,
    revision: 1,
    deviceId: serialNumber,
    capabilities: ["action-programs-v1", "state-overlays-v1"],
    actionPrograms: Array.from({ length: ACTION_PROGRAM_COUNT }, defaultActionProgram),
    actionProgramNames: Array.from({ length: ACTION_PROGRAM_COUNT }, (_, i) => `Macro ${i + 1}`),
    actionOverlays: Array.from({ length: ACTION_OVERLAY_COUNT }, defaultActionOverlayBinding),
    actionOverlayNames: Array.from({ length: ACTION_OVERLAY_COUNT }, (_, i) => `Overlay ${i + 1}`),
    actionStateBits: profileIndex,
    keySettings: Array.from({ length: LAYER_COUNT }, (_, layerIndex) => (
      Array.from({ length: KEY_COUNT }, (_, keyIndex) => ({
        key_index: keyIndex,
        profile_index: profileIndex,
        layer_index: layerIndex,
        hid_keycode: 4 + keyIndex,
        actuation_point_mm: 1,
        release_point_mm: 1,
        rapid_trigger_press: 0.1,
        rapid_trigger_release: 0.1,
        socd_pair: null,
        socd_resolution: 0,
        rapid_trigger_enabled: false,
        continuous_rapid_trigger: false,
        behavior_mode: 0,
        hold_threshold_ms: 200,
        secondary_hid_keycode: 0,
        dynamic_zones: [],
        tap_hold_options: 0,
        dks_bottom_out_point_mm: 4,
        socd_fully_pressed_enabled: false,
        socd_fully_pressed_point_mm: 4,
        disable_kb_on_gamepad: false,
      }))
    )).flat(),
    keyGamepadMaps: [],
    gamepadSettings: {
      deadzone: 4, keyboard_routing: 0, square_mode: false, reactive_stick: false, api_mode: 0,
      curve_points: [
        { x_01mm: 0, x_mm: 0, y: 0 },
        { x_01mm: 100, x_mm: 1, y: 85 },
        { x_01mm: 200, x_mm: 2, y: 170 },
        { x_01mm: 400, x_mm: 4, y: 255 },
      ],
    },
    rotarySettings: {
      rotation_action: 0, button_action: 0, sensitivity: 1, acceleration: 0,
      step_size: 1, invert_direction: false, rgb_behavior: 0, rgb_effect_mode: 4,
      progress_style: 0, progress_effect_mode: 1, progress_color: [40, 210, 64],
      progress_filled_only: false, cw_binding: binding, ccw_binding: binding, click_binding: binding,
    },
    filterEnabled: true,
    filterParams: { noise_band: 8, alpha_min_denom: 16, alpha_max_denom: 2 },
    options: {
      keyboard_enabled: true, gamepad_enabled: false, raw_hid_echo: false,
      led_thermal_protection_enabled: true,
    },
    nkroEnabled: true,
    advancedTickRate: 8,
    led: {
      enabled: true,
      brightness: 128,
      pixels: Array(KEY_COUNT * 3).fill(profileIndex),
      effectMode: 1,
      fpsLimit: 60,
      effectParams: Array.from(
        { length: LED_EFFECT_COUNT },
        () => Array(LED_EFFECT_PARAM_COUNT).fill(0),
      ),
      idleOptions: {
        idle_timeout_seconds: 0, allow_system_when_disabled: false,
        third_party_stream_counts_as_activity: true, usb_suspend_rgb_off: true,
      },
      triggerChatterGuard: { enabled: false, duration_ms: 0 },
    },
  };
}

class MemoryStore implements CalibrationMigrationStore {
  readonly values = new Map<string, unknown>();
  failSave = false;
  async init() {}
  async get(key: string) { return this.values.get(key); }
  async set(key: string, value: unknown) { this.values.set(key, structuredClone(value)); }
  async delete(key: string) { return this.values.delete(key); }
  async entries() { return Array.from(this.values.entries()); }
  async save() { if (this.failSave) throw new Error("disk unavailable"); }
}

class MemoryProfileApi implements FirmwareProfileMigrationApi {
  snapshots = new Map<number, FirmwareProfileSnapshot>();
  applied: number[] = [];
  corruptVerification = false;
  async capture(profileIndex: number) {
    const snapshot = this.snapshots.get(profileIndex);
    if (!snapshot) return null;
    const captured = structuredClone(snapshot);
    if (this.corruptVerification && this.applied.length > 0) captured.keySettings[0]!.hid_keycode += 1;
    return captured;
  }
  async apply(snapshot: FirmwareProfileSnapshot, targetProfileIndex: number) {
    // Production intentionally strips globals from per-profile apply. The fake
    // restores them from its existing state so verification models live reads.
    const previous = this.snapshots.get(targetProfileIndex);
    this.snapshots.set(targetProfileIndex, {
      ...structuredClone(snapshot),
      options: previous?.options ?? profileSnapshot(targetProfileIndex, snapshot.deviceId!).options,
      nkroEnabled: previous?.nkroEnabled ?? true,
    });
    this.applied.push(targetProfileIndex);
    return true;
  }
}

class MemoryDevice implements UpdaterMigrationDevice {
  ramOnly = false;
  globalOptions = profileSnapshot(0, "fixture").options!;
  nkroEnabled = true;
  constructor(
    public current: CalibrationSettings | null,
    public usedMask: number,
    public activeProfileIndex: number,
    public defaultProfileIndex: number,
    public names: Array<string | null>,
  ) {}
  async getCalibration() { return this.current ? structuredClone(this.current) : null; }
  async setCalibration(lutZero: number, keyZeros: ArrayLike<number>, keyMaxs: ArrayLike<number>) {
    this.current = { lut_zero_value: lutZero, key_zero_values: Array.from(keyZeros), key_max_values: Array.from(keyMaxs) };
    return true;
  }
  async getActiveProfile() { return { profile_index: this.activeProfileIndex, profile_used_mask: this.usedMask }; }
  async getDefaultProfile() { return { profile_index: this.defaultProfileIndex, profile_used_mask: this.usedMask }; }
  async getProfileName(index: number) {
    const name = this.names[index];
    return name == null ? null : { name, profile_used_mask: this.usedMask };
  }
  async createProfile(name?: string) {
    const index = Array.from({ length: SETTINGS_PROFILE_COUNT }, (_, i) => i)
      .find((candidate) => !(this.usedMask & (1 << candidate)));
    if (index == null) return null;
    this.usedMask |= 1 << index;
    this.names[index] = name ?? `Profile ${index + 1}`;
    return { profile_index: index, profile_used_mask: this.usedMask };
  }
  async resetProfileSlot(index: number) {
    return this.usedMask & (1 << index) ? { profile_index: index, profile_used_mask: this.usedMask } : null;
  }
  async deleteProfile(index: number) {
    if (!(this.usedMask & (1 << index))) return null;
    this.usedMask &= ~(1 << index);
    this.names[index] = null;
    return { profile_index: this.activeProfileIndex, profile_used_mask: this.usedMask };
  }
  async setProfileName(index: number, name: string) {
    if (!(this.usedMask & (1 << index))) return null;
    this.names[index] = name;
    return { name };
  }
  async setActiveProfile(index: number) {
    if (!(this.usedMask & (1 << index))) return null;
    this.activeProfileIndex = index;
    return { profile_index: index, profile_used_mask: this.usedMask };
  }
  async setDefaultProfile(index: number) {
    if (index !== 0xff && !(this.usedMask & (1 << index))) return null;
    this.defaultProfileIndex = index;
    return { profile_index: index, profile_used_mask: this.usedMask };
  }
  async saveSettings() { return true; }
  async getRamOnlyMode() { return this.ramOnly; }
  async setOptions(options: NonNullable<FirmwareProfileSnapshot["options"]>) {
    this.globalOptions = structuredClone(options);
    return true;
  }
  async setNkroEnabled(enabled: boolean) { this.nkroEnabled = enabled; return true; }
}

function originalDevice(serial: string) {
  const device = new MemoryDevice(calibration(), 0b0101, 2, 0, ["Main", null, "Game", null]);
  const profiles = new MemoryProfileApi();
  profiles.snapshots.set(0, profileSnapshot(0, serial));
  profiles.snapshots.set(2, profileSnapshot(2, serial));
  return { device, profiles };
}

describe("complete updater migration recovery", () => {
  test("validates calibration and detects only the exact reset values", () => {
    const invalid = calibration();
    invalid.key_zero_values.pop();
    expect(normalizeCalibrationSettings(invalid)).toBeNull();
    const reset = { lut_zero_value: 2195, key_zero_values: Array(KEY_COUNT).fill(2195), key_max_values: Array(KEY_COUNT).fill(2850) };
    expect(isResetCalibration(reset)).toBeTrue();
    reset.key_zero_values[37] = 2170;
    expect(isResetCalibration(reset)).toBeFalse();
  });

  test("captures and restores calibration, globals and exact four-slot topology", async () => {
    const serial = "KBHE-123";
    const store = new MemoryStore();
    const original = originalDevice(serial);
    const backup = await captureUpdaterMigrationBackup(serial, original.device, store, original.profiles);
    expect(hasCompleteProfileRecovery(backup)).toBeTrue();
    expect(backup.profiles.usedMask).toBe(0b0101);
    const migrated = new MemoryDevice(calibration(100), 0b0001, 0, 0, ["Default", null, null, null]);
    const migratedProfiles = new MemoryProfileApi();
    migratedProfiles.snapshots.set(0, profileSnapshot(0, serial));
    expect(await restoreUpdaterMigrationBackup(serial, migrated, store, migratedProfiles)).toBeTrue();
    expect(migrated.current).toEqual(calibration());
    expect(migrated.usedMask).toBe(0b0101);
    expect(migrated.names).toEqual(["Main", null, "Game", null]);
    expect([migrated.activeProfileIndex, migrated.defaultProfileIndex]).toEqual([2, 0]);
    expect(migratedProfiles.applied).toEqual([0, 2]);
    expect(await getUpdaterMigrationBackup(serial, store)).toBeNull();
  });

  test("keeps backup when semantic verification fails", async () => {
    const serial = "KBHE-VERIFY";
    const store = new MemoryStore();
    const original = originalDevice(serial);
    await captureUpdaterMigrationBackup(serial, original.device, store, original.profiles);
    const migrated = new MemoryDevice(calibration(100), 1, 0, 0, ["Default", null, null, null]);
    const profiles = new MemoryProfileApi();
    profiles.snapshots.set(0, profileSnapshot(0, serial));
    profiles.corruptVerification = true;
    await expect(restoreUpdaterMigrationBackup(serial, migrated, store, profiles))
      .rejects.toThrow("SETTINGS_RESTORE_REQUIRED");
    expect(await getUpdaterMigrationBackup(serial, store)).not.toBeNull();
  });

  test("restores legacy calibration-only data but never considers it complete", async () => {
    const serial = "KBHE-LEGACY";
    const store = new MemoryStore();
    store.values.set(`calibration:${serial}`, { schemaVersion: 1, serialNumber: serial, capturedAt: 1, calibration: calibration() });
    expect(hasCompleteProfileRecovery(await getUpdaterMigrationBackup(serial, store))).toBeFalse();
    const runtime = originalDevice(serial);
    runtime.device.current = calibration(100);
    expect(await restoreUpdaterMigrationBackup(serial, runtime.device, store, runtime.profiles)).toBeTrue();
    expect(runtime.device.current).toEqual(calibration());
    expect(runtime.profiles.applied).toEqual([]);
  });

  test("blocks RAM-only app profiles and incomplete used slots", async () => {
    const serial = "KBHE-BLOCKED";
    const original = originalDevice(serial);
    original.device.ramOnly = true;
    await expect(captureUpdaterMigrationBackup(serial, original.device, new MemoryStore(), original.profiles))
      .rejects.toThrow("RAM-only");
    original.device.ramOnly = false;
    original.profiles.snapshots.delete(2);
    await expect(captureUpdaterMigrationBackup(serial, original.device, new MemoryStore(), original.profiles))
      .rejects.toThrow("SETTINGS_BACKUP_REQUIRED");
  });

  test("does not accept an in-memory backup when durable save fails", async () => {
    const serial = "KBHE-NOSAVE";
    const store = new MemoryStore();
    store.failSave = true;
    const original = originalDevice(serial);
    await expect(captureUpdaterMigrationBackup(serial, original.device, store, original.profiles))
      .rejects.toThrow("SETTINGS_BACKUP_REQUIRED");
    store.failSave = false;
    expect(await getUpdaterMigrationBackup(serial, store)).toBeNull();
  });
});
