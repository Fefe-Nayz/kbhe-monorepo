import { LazyStore } from "@tauri-apps/plugin-store";
import { kbheDevice, type CalibrationSettings } from "./device";
import { KEY_COUNT } from "./protocol";

const STORE_PATH = "updater-migration-recovery.json";
const STORE_KEY_PREFIX = "calibration:";
const SCHEMA_VERSION = 1 as const;
const ADC_MIN = 0;
const ADC_MAX = 4095;
const RESET_ZERO = 2195;
const RESET_MAX = 2850;

export const CALIBRATION_MIGRATION_QUERY_KEY = ["calibration", "updaterMigrationRecovery"] as const;

export interface CalibrationMigrationBackup {
  schemaVersion: typeof SCHEMA_VERSION;
  serialNumber: string;
  capturedAt: number;
  calibration: CalibrationSettings;
}

export interface CalibrationMigrationDevice {
  getCalibration(): Promise<CalibrationSettings | null>;
  setCalibration(
    lutZero: number,
    keyZeros: ArrayLike<number>,
    keyMaxs: ArrayLike<number>,
  ): Promise<boolean>;
}

export interface CalibrationMigrationStore {
  init(): Promise<void>;
  get(key: string): Promise<unknown>;
  set(key: string, value: unknown): Promise<void>;
  delete(key: string): Promise<boolean>;
  entries(): Promise<Array<[string, unknown]>>;
  save(): Promise<void>;
}

const lazyStore = new LazyStore(STORE_PATH, { autoSave: false, defaults: {} });
const persistentStore: CalibrationMigrationStore = {
  init: () => lazyStore.init(),
  get: (key) => lazyStore.get<unknown>(key),
  set: (key, value) => lazyStore.set(key, value),
  delete: (key) => lazyStore.delete(key),
  entries: () => lazyStore.entries<unknown>(),
  save: () => lazyStore.save(),
};

function normalizedSerialNumber(serialNumber: string): string {
  const normalized = serialNumber.trim();
  if (!normalized) {
    throw new Error("CALIBRATION_BACKUP_REQUIRED: keyboard serial number is missing");
  }
  return normalized;
}

function backupKey(serialNumber: string): string {
  return `${STORE_KEY_PREFIX}${encodeURIComponent(normalizedSerialNumber(serialNumber))}`;
}

function normalizeAdcValue(value: unknown): number | null {
  return Number.isInteger(value) && Number(value) >= ADC_MIN && Number(value) <= ADC_MAX
    ? Number(value)
    : null;
}

export function normalizeCalibrationSettings(value: unknown): CalibrationSettings | null {
  if (!value || typeof value !== "object") return null;
  const candidate = value as Partial<CalibrationSettings>;
  const lutZero = normalizeAdcValue(candidate.lut_zero_value);
  if (
    lutZero == null
    || !Array.isArray(candidate.key_zero_values)
    || !Array.isArray(candidate.key_max_values)
    || candidate.key_zero_values.length !== KEY_COUNT
    || candidate.key_max_values.length !== KEY_COUNT
  ) {
    return null;
  }

  const keyZeros = candidate.key_zero_values.map(normalizeAdcValue);
  const keyMaxs = candidate.key_max_values.map(normalizeAdcValue);
  if (keyZeros.some((item) => item == null) || keyMaxs.some((item) => item == null)) {
    return null;
  }

  return {
    lut_zero_value: lutZero,
    key_zero_values: keyZeros as number[],
    key_max_values: keyMaxs as number[],
  };
}

export function isResetCalibration(value: unknown): boolean {
  const calibration = normalizeCalibrationSettings(value);
  return Boolean(
    calibration
    && calibration.lut_zero_value === RESET_ZERO
    && calibration.key_zero_values.every((item) => item === RESET_ZERO)
    && calibration.key_max_values.every((item) => item === RESET_MAX),
  );
}

function normalizeBackup(value: unknown): CalibrationMigrationBackup | null {
  if (!value || typeof value !== "object") return null;
  const candidate = value as Partial<CalibrationMigrationBackup>;
  const serialNumber = typeof candidate.serialNumber === "string"
    ? candidate.serialNumber.trim()
    : "";
  const calibration = normalizeCalibrationSettings(candidate.calibration);
  if (
    candidate.schemaVersion !== SCHEMA_VERSION
    || !serialNumber
    || !Number.isSafeInteger(candidate.capturedAt)
    || Number(candidate.capturedAt) <= 0
    || !calibration
  ) {
    return null;
  }
  return {
    schemaVersion: SCHEMA_VERSION,
    serialNumber,
    capturedAt: Number(candidate.capturedAt),
    calibration,
  };
}

function calibrationsEqual(left: CalibrationSettings, right: CalibrationSettings): boolean {
  return left.lut_zero_value === right.lut_zero_value
    && left.key_zero_values.every((value, index) => value === right.key_zero_values[index])
    && left.key_max_values.every((value, index) => value === right.key_max_values[index]);
}

export async function getCalibrationMigrationBackup(
  serialNumber: string,
  store: CalibrationMigrationStore = persistentStore,
): Promise<CalibrationMigrationBackup | null> {
  const serial = normalizedSerialNumber(serialNumber);
  await store.init();
  const raw = await store.get(backupKey(serial));
  if (raw == null) return null;
  const backup = normalizeBackup(raw);
  if (!backup || backup.serialNumber !== serial) {
    throw new Error(
      "CALIBRATION_BACKUP_INVALID: the saved pre-migration calibration is corrupt or belongs to another keyboard",
    );
  }
  return backup;
}

export async function listCalibrationMigrationBackups(
  store: CalibrationMigrationStore = persistentStore,
): Promise<CalibrationMigrationBackup[]> {
  await store.init();
  const backups: CalibrationMigrationBackup[] = [];
  for (const [key, raw] of await store.entries()) {
    if (!key.startsWith(STORE_KEY_PREFIX)) continue;
    const backup = normalizeBackup(raw);
    if (!backup || backupKey(backup.serialNumber) !== key) {
      throw new Error(
        "CALIBRATION_BACKUP_INVALID: a saved pre-migration calibration is corrupt",
      );
    }
    backups.push(backup);
  }
  return backups.sort((left, right) => right.capturedAt - left.capturedAt);
}

export async function captureCalibrationMigrationBackup(
  serialNumber: string,
  device: CalibrationMigrationDevice = kbheDevice,
  store: CalibrationMigrationStore = persistentStore,
): Promise<CalibrationMigrationBackup> {
  const serial = normalizedSerialNumber(serialNumber);
  const calibration = normalizeCalibrationSettings(await device.getCalibration());
  if (!calibration) {
    throw new Error(
      "CALIBRATION_BACKUP_REQUIRED: the keyboard's complete 82-key calibration could not be read; updater migration was not started",
    );
  }

  const backup: CalibrationMigrationBackup = {
    schemaVersion: SCHEMA_VERSION,
    serialNumber: serial,
    capturedAt: Date.now(),
    calibration,
  };
  await store.init();
  const key = backupKey(serial);
  await store.set(key, backup);
  try {
    await store.save();
  } catch (error) {
    // A value present only in the plugin's memory is not a recovery backup.
    // Remove it so a retry in this process cannot mistake it for durable data.
    try {
      await store.delete(key);
    } catch {
      // The original persistence failure is the actionable error.
    }
    throw new Error(
      `CALIBRATION_BACKUP_REQUIRED: the pre-migration calibration could not be saved; updater migration was not started (${error instanceof Error ? error.message : String(error)})`,
      { cause: error },
    );
  }

  const persisted = normalizeBackup(await store.get(key));
  if (!persisted || persisted.serialNumber !== serial || !calibrationsEqual(calibration, persisted.calibration)) {
    throw new Error(
      "CALIBRATION_BACKUP_REQUIRED: the pre-migration calibration could not be persisted safely; updater migration was not started",
    );
  }
  return persisted;
}

export async function restoreCalibrationMigrationBackup(
  serialNumber: string,
  device: CalibrationMigrationDevice = kbheDevice,
  store: CalibrationMigrationStore = persistentStore,
): Promise<boolean> {
  const serial = normalizedSerialNumber(serialNumber);
  const backup = await getCalibrationMigrationBackup(serial, store);
  if (!backup) return false;

  const written = await device.setCalibration(
    backup.calibration.lut_zero_value,
    backup.calibration.key_zero_values,
    backup.calibration.key_max_values,
  );
  if (!written) {
    throw new Error(
      "CALIBRATION_RESTORE_REQUIRED: firmware updated, but the saved calibration could not be written; the backup was kept for retry",
    );
  }

  const verified = normalizeCalibrationSettings(await device.getCalibration());
  if (!verified || !calibrationsEqual(backup.calibration, verified)) {
    throw new Error(
      "CALIBRATION_RESTORE_REQUIRED: calibration verification failed after the update; the backup was kept for retry",
    );
  }

  await store.delete(backupKey(serial));
  await store.save();
  return true;
}
