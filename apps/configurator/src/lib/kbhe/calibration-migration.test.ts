import { describe, expect, test } from "bun:test";
import {
  captureCalibrationMigrationBackup,
  getCalibrationMigrationBackup,
  isResetCalibration,
  normalizeCalibrationSettings,
  restoreCalibrationMigrationBackup,
  type CalibrationMigrationDevice,
  type CalibrationMigrationStore,
} from "./calibration-migration";
import { KEY_COUNT } from "./protocol";
import type { CalibrationSettings } from "./device";

function calibration(offset = 0): CalibrationSettings {
  return {
    lut_zero_value: 2195 + offset,
    key_zero_values: Array.from({ length: KEY_COUNT }, (_, index) => 2140 + index % 45 + offset),
    key_max_values: Array.from({ length: KEY_COUNT }, (_, index) => 2820 + index % 35 + offset),
  };
}

class MemoryStore implements CalibrationMigrationStore {
  readonly values = new Map<string, unknown>();
  saves = 0;
  failSave = false;

  async init() {}
  async get(key: string) { return this.values.get(key); }
  async set(key: string, value: unknown) { this.values.set(key, structuredClone(value)); }
  async delete(key: string) { return this.values.delete(key); }
  async entries() { return Array.from(this.values.entries()); }
  async save() {
    if (this.failSave) throw new Error("disk unavailable");
    this.saves += 1;
  }
}

class MemoryDevice implements CalibrationMigrationDevice {
  constructor(public current: CalibrationSettings | null) {}

  async getCalibration() {
    return this.current ? structuredClone(this.current) : null;
  }

  async setCalibration(lutZero: number, keyZeros: ArrayLike<number>, keyMaxs: ArrayLike<number>) {
    this.current = {
      lut_zero_value: lutZero,
      key_zero_values: Array.from(keyZeros),
      key_max_values: Array.from(keyMaxs),
    };
    return true;
  }
}

describe("updater migration calibration recovery", () => {
  test("requires a complete 82-key ADC calibration", () => {
    const invalid = calibration();
    invalid.key_zero_values.pop();
    expect(normalizeCalibrationSettings(invalid)).toBeNull();
    expect(normalizeCalibrationSettings(calibration())).toEqual(calibration());
  });

  test("detects the exact reset calibration without hiding real per-key data", () => {
    const reset: CalibrationSettings = {
      lut_zero_value: 2195,
      key_zero_values: Array(KEY_COUNT).fill(2195),
      key_max_values: Array(KEY_COUNT).fill(2850),
    };
    expect(isResetCalibration(reset)).toBeTrue();
    reset.key_zero_values[37] = 2170;
    expect(isResetCalibration(reset)).toBeFalse();
  });

  test("persists before migration and deletes only after verified restoration", async () => {
    const store = new MemoryStore();
    const original = calibration();
    const device = new MemoryDevice(original);

    await captureCalibrationMigrationBackup("KBHE-123", device, store);
    expect(store.saves).toBe(1);
    expect(await getCalibrationMigrationBackup("KBHE-123", store)).not.toBeNull();

    device.current = calibration(100);
    expect(await restoreCalibrationMigrationBackup("KBHE-123", device, store)).toBeTrue();
    expect(device.current).toEqual(original);
    expect(await getCalibrationMigrationBackup("KBHE-123", store)).toBeNull();
    expect(store.saves).toBe(2);
  });

  test("keeps the durable backup when post-write verification fails", async () => {
    const store = new MemoryStore();
    const original = calibration();
    await captureCalibrationMigrationBackup("KBHE-456", new MemoryDevice(original), store);

    const device: CalibrationMigrationDevice = {
      getCalibration: async () => calibration(1),
      setCalibration: async () => true,
    };
    await expect(restoreCalibrationMigrationBackup("KBHE-456", device, store))
      .rejects.toThrow("CALIBRATION_RESTORE_REQUIRED");
    expect(await getCalibrationMigrationBackup("KBHE-456", store)).not.toBeNull();
  });

  test("blocks migration when the calibration cannot be read", async () => {
    await expect(captureCalibrationMigrationBackup(
      "KBHE-789",
      new MemoryDevice(null),
      new MemoryStore(),
    )).rejects.toThrow("CALIBRATION_BACKUP_REQUIRED");
  });

  test("does not accept an in-memory record when durable save fails", async () => {
    const store = new MemoryStore();
    store.failSave = true;
    await expect(captureCalibrationMigrationBackup(
      "KBHE-NOSAVE",
      new MemoryDevice(calibration()),
      store,
    )).rejects.toThrow("CALIBRATION_BACKUP_REQUIRED");
    store.failSave = false;
    expect(await getCalibrationMigrationBackup("KBHE-NOSAVE", store)).toBeNull();
  });
});
