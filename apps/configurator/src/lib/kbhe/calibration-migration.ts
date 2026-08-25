import { LazyStore } from "@tauri-apps/plugin-store";
import { kbheDevice, type CalibrationSettings, type ProfileInfo } from "./device";
import { KEY_COUNT, LAYER_COUNT, SETTINGS_PROFILE_COUNT } from "./protocol";
import {
  applyFirmwareProfileSnapshot,
  captureFirmwareProfileSnapshot,
  parseFirmwareProfileSnapshot,
  type FirmwareProfileSnapshot,
} from "./profile-sync";

const STORE_PATH = "updater-migration-recovery.json";
const STORE_KEY_PREFIX = "calibration:";
const LEGACY_SCHEMA_VERSION = 1 as const;
const PROFILE_SCHEMA_VERSION = 2 as const;
const COMPLETE_SCHEMA_VERSION = 3 as const;
const ADC_MIN = 0;
const ADC_MAX = 4095;
const RESET_ZERO = 2195;
const RESET_MAX = 2850;

export const UPDATER_MIGRATION_QUERY_KEY = ["calibration", "updaterMigrationRecovery"] as const;

interface BaseMigrationBackup {
  serialNumber: string;
  capturedAt: number;
  calibration: CalibrationSettings;
}

export interface LegacyCalibrationMigrationBackup extends BaseMigrationBackup {
  schemaVersion: typeof LEGACY_SCHEMA_VERSION;
}

export interface FirmwareProfilesMigrationBackup {
  usedMask: number;
  activeProfileIndex: number;
  defaultProfileIndex: number;
  globals: {
    options: NonNullable<FirmwareProfileSnapshot["options"]>;
    nkroEnabled: boolean;
  };
  names: Array<string | null>;
  snapshots: Array<FirmwareProfileSnapshot | null>;
}

export interface LegacyProfilesMigrationBackup extends BaseMigrationBackup {
  schemaVersion: typeof PROFILE_SCHEMA_VERSION;
  profiles: FirmwareProfilesMigrationBackup;
}

export interface CompleteUpdaterMigrationBackup extends BaseMigrationBackup {
  schemaVersion: typeof COMPLETE_SCHEMA_VERSION;
  keyboardName: string;
  profiles: FirmwareProfilesMigrationBackup;
}

export type UpdaterMigrationBackup =
  | LegacyCalibrationMigrationBackup
  | LegacyProfilesMigrationBackup
  | CompleteUpdaterMigrationBackup;

export interface UpdaterMigrationDevice {
  getCalibration(): Promise<CalibrationSettings | null>;
  getKeyboardName(): Promise<string | null>;
  setKeyboardName(name: string): Promise<string | null>;
  setCalibration(
    lutZero: number,
    keyZeros: ArrayLike<number>,
    keyMaxs: ArrayLike<number>,
  ): Promise<boolean>;
  getActiveProfile(): Promise<ProfileInfo | null>;
  getDefaultProfile(): Promise<{ profile_index: number; profile_used_mask: number } | null>;
  getProfileName(profileIndex: number): Promise<{ name: string; profile_used_mask: number } | null>;
  createProfile(name?: string): Promise<ProfileInfo | null>;
  resetProfileSlot(profileIndex: number): Promise<ProfileInfo | null>;
  deleteProfile(profileIndex: number): Promise<ProfileInfo | null>;
  setProfileName(profileIndex: number, name: string): Promise<{ name: string } | null>;
  setActiveProfile(profileIndex: number): Promise<ProfileInfo | null>;
  setDefaultProfile(profileIndex: number): Promise<{ profile_index: number; profile_used_mask: number } | null>;
  saveSettings(): Promise<boolean>;
  getRamOnlyMode(): Promise<boolean | null>;
  setOptions(options: NonNullable<FirmwareProfileSnapshot["options"]>): Promise<boolean>;
  setNkroEnabled(enabled: boolean): Promise<boolean>;
}

export interface FirmwareProfileMigrationApi {
  capture(profileIndex: number): Promise<FirmwareProfileSnapshot | null>;
  apply(
    snapshot: FirmwareProfileSnapshot,
    targetProfileIndex: number,
    options: { persistToFlash: boolean; restoreActiveProfile: boolean },
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
const firmwareProfileApi: FirmwareProfileMigrationApi = {
  capture: (profileIndex) => captureFirmwareProfileSnapshot(profileIndex),
  apply: (snapshot, profileIndex, options) => (
    applyFirmwareProfileSnapshot(snapshot, profileIndex, options)
  ),
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

function normalizeKeyboardName(value: unknown): string | null {
  if (typeof value !== "string" || value.length === 0 || value.length > 32) return null;
  if (value.endsWith(" ")) return null;
  return Array.from(value).every((character) => {
    const code = character.charCodeAt(0);
    return code >= 0x20 && code <= 0x7e;
  }) ? value : null;
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

function normalizeCompleteProfileSnapshot(
  value: unknown,
  profileIndex: number,
  serialNumber: string,
): FirmwareProfileSnapshot | null {
  // Firmware predating the action-program extension reports no action
  // capabilities. Some intermediate configurators represented the unsupported
  // collections as [] rather than null; canonicalize those placeholders before
  // applying the strict profile schema.
  let candidate = value;
  if (value && typeof value === "object") {
    const raw = value as Partial<FirmwareProfileSnapshot>;
    const capabilities = Array.isArray(raw.capabilities) ? raw.capabilities : [];
    const supportsPrograms = capabilities.includes("action-programs-v1");
    const supportsOverlays = capabilities.includes("state-overlays-v1");
    if (!supportsPrograms || !supportsOverlays) {
      const canonical = { ...raw } as Partial<FirmwareProfileSnapshot>;
      if (!supportsPrograms) {
        if (Array.isArray(canonical.actionPrograms) && canonical.actionPrograms.length === 0) {
          canonical.actionPrograms = null;
        }
        if (Array.isArray(canonical.actionProgramNames) && canonical.actionProgramNames.length === 0) {
          delete canonical.actionProgramNames;
        }
      }
      if (!supportsOverlays) {
        if (Array.isArray(canonical.actionOverlays) && canonical.actionOverlays.length === 0) {
          canonical.actionOverlays = null;
        }
        if (Array.isArray(canonical.actionOverlayNames) && canonical.actionOverlayNames.length === 0) {
          delete canonical.actionOverlayNames;
        }
      }
      candidate = canonical;
    }
  }

  const snapshot = parseFirmwareProfileSnapshot(candidate);
  const capabilities = snapshot?.capabilities ?? [];
  const supportsPrograms = capabilities.includes("action-programs-v1");
  const supportsOverlays = capabilities.includes("state-overlays-v1");
  if (
    !snapshot
    || snapshot.schemaVersion !== 2
    || snapshot.sourceProfileIndex !== profileIndex
    || snapshot.deviceId !== serialNumber
    || snapshot.keySettings.length !== KEY_COUNT * LAYER_COUNT
    || snapshot.keyGamepadMaps == null
    || snapshot.gamepadSettings == null
    || snapshot.rotarySettings == null
    || snapshot.filterEnabled == null
    || snapshot.filterParams == null
    || snapshot.options == null
    || typeof snapshot.options.led_thermal_protection_enabled !== "boolean"
    || snapshot.nkroEnabled == null
    || snapshot.advancedTickRate == null
    || (supportsPrograms && (
      snapshot.actionPrograms == null
      || snapshot.actionProgramNames == null
    ))
    || (!supportsPrograms && snapshot.actionPrograms != null)
    || (supportsOverlays && (
      snapshot.actionOverlays == null
      || snapshot.actionOverlayNames == null
      || snapshot.actionStateBits == null
    ))
    || (!supportsOverlays && (
      snapshot.actionOverlays != null
      || snapshot.actionStateBits != null
    ))
    || snapshot.led == null
    || snapshot.led.enabled == null
    || snapshot.led.brightness == null
    || snapshot.led.pixels == null
    || snapshot.led.effectMode == null
    || snapshot.led.fpsLimit == null
    || snapshot.led.effectParams == null
    || snapshot.led.idleOptions == null
    || snapshot.led.triggerChatterGuard == null
  ) {
    return null;
  }
  return snapshot;
}

function normalizeProfilesBackup(
  value: unknown,
  serialNumber: string,
): FirmwareProfilesMigrationBackup | null {
  if (!value || typeof value !== "object") return null;
  const candidate = value as Partial<FirmwareProfilesMigrationBackup>;
  const globalsCandidate = candidate.globals as Partial<FirmwareProfilesMigrationBackup["globals"]> | undefined;
  const globalOptions = globalsCandidate?.options;
  if (
    !Number.isInteger(candidate.usedMask)
    || Number(candidate.usedMask) <= 0
    || Number(candidate.usedMask) >= (1 << SETTINGS_PROFILE_COUNT)
    || !Number.isInteger(candidate.activeProfileIndex)
    || !Number.isInteger(candidate.defaultProfileIndex)
    || !Array.isArray(candidate.names)
    || candidate.names.length !== SETTINGS_PROFILE_COUNT
    || !Array.isArray(candidate.snapshots)
    || candidate.snapshots.length !== SETTINGS_PROFILE_COUNT
    || !globalOptions
    || typeof globalOptions.keyboard_enabled !== "boolean"
    || typeof globalOptions.gamepad_enabled !== "boolean"
    || typeof globalOptions.raw_hid_echo !== "boolean"
    || typeof globalOptions.led_thermal_protection_enabled !== "boolean"
    || typeof globalsCandidate?.nkroEnabled !== "boolean"
  ) {
    return null;
  }

  const usedMask = Number(candidate.usedMask);
  const activeProfileIndex = Number(candidate.activeProfileIndex);
  const defaultProfileIndex = Number(candidate.defaultProfileIndex);
  if (
    activeProfileIndex < 0
    || activeProfileIndex >= SETTINGS_PROFILE_COUNT
    || !(usedMask & (1 << activeProfileIndex))
    || (defaultProfileIndex !== 0xff && (
      defaultProfileIndex < 0
      || defaultProfileIndex >= SETTINGS_PROFILE_COUNT
      || !(usedMask & (1 << defaultProfileIndex))
    ))
  ) {
    return null;
  }

  const names: Array<string | null> = [];
  const snapshots: Array<FirmwareProfileSnapshot | null> = [];
  for (let profileIndex = 0; profileIndex < SETTINGS_PROFILE_COUNT; profileIndex += 1) {
    const used = Boolean(usedMask & (1 << profileIndex));
    const name = candidate.names[profileIndex];
    const snapshot = candidate.snapshots[profileIndex];
    if (!used) {
      if (name != null || snapshot != null) return null;
      names.push(null);
      snapshots.push(null);
      continue;
    }
    if (typeof name !== "string") return null;
    const completeSnapshot = normalizeCompleteProfileSnapshot(snapshot, profileIndex, serialNumber);
    if (
      !completeSnapshot
      || JSON.stringify(completeSnapshot.options) !== JSON.stringify(globalOptions)
      || completeSnapshot.nkroEnabled !== globalsCandidate.nkroEnabled
    ) return null;
    names.push(name);
    snapshots.push(completeSnapshot);
  }

  return {
    usedMask,
    activeProfileIndex,
    defaultProfileIndex,
    globals: {
      options: {
        keyboard_enabled: globalOptions.keyboard_enabled,
        gamepad_enabled: globalOptions.gamepad_enabled,
        raw_hid_echo: globalOptions.raw_hid_echo,
        led_thermal_protection_enabled: globalOptions.led_thermal_protection_enabled,
      },
      nkroEnabled: globalsCandidate.nkroEnabled,
    },
    names,
    snapshots,
  };
}

export function hasCompleteProfileRecovery(
  backup: UpdaterMigrationBackup | null,
): backup is CompleteUpdaterMigrationBackup {
  return backup?.schemaVersion === COMPLETE_SCHEMA_VERSION;
}

export function hasProfileRecovery(
  backup: UpdaterMigrationBackup | null,
): backup is LegacyProfilesMigrationBackup | CompleteUpdaterMigrationBackup {
  return backup?.schemaVersion === PROFILE_SCHEMA_VERSION
    || backup?.schemaVersion === COMPLETE_SCHEMA_VERSION;
}

function normalizeBackup(value: unknown): UpdaterMigrationBackup | null {
  if (!value || typeof value !== "object") return null;
  const candidate = value as Partial<UpdaterMigrationBackup>;
  const serialNumber = typeof candidate.serialNumber === "string"
    ? candidate.serialNumber.trim()
    : "";
  const calibration = normalizeCalibrationSettings(candidate.calibration);
  if (
    (candidate.schemaVersion !== LEGACY_SCHEMA_VERSION
      && candidate.schemaVersion !== PROFILE_SCHEMA_VERSION
      && candidate.schemaVersion !== COMPLETE_SCHEMA_VERSION)
    || !serialNumber
    || !Number.isSafeInteger(candidate.capturedAt)
    || Number(candidate.capturedAt) <= 0
    || !calibration
  ) {
    return null;
  }
  if (
    candidate.schemaVersion === PROFILE_SCHEMA_VERSION
    || candidate.schemaVersion === COMPLETE_SCHEMA_VERSION
  ) {
    const profiles = normalizeProfilesBackup(
      (candidate as { profiles?: unknown }).profiles,
      serialNumber,
    );
    if (!profiles) return null;
    if (candidate.schemaVersion === PROFILE_SCHEMA_VERSION) {
      return {
        schemaVersion: PROFILE_SCHEMA_VERSION,
        serialNumber,
        capturedAt: Number(candidate.capturedAt),
        calibration,
        profiles,
      };
    }
    const keyboardName = normalizeKeyboardName(
      (candidate as Partial<CompleteUpdaterMigrationBackup>).keyboardName,
    );
    if (!keyboardName) return null;
    return {
      schemaVersion: COMPLETE_SCHEMA_VERSION,
      serialNumber,
      capturedAt: Number(candidate.capturedAt),
      calibration,
      keyboardName,
      profiles,
    };
  }
  return {
    schemaVersion: LEGACY_SCHEMA_VERSION,
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

function snapshotSemantics(
  snapshot: FirmwareProfileSnapshot,
  sourceSnapshot: FirmwareProfileSnapshot,
): unknown {
  const semantic = { ...snapshot } as Record<string, unknown>;
  delete semantic.capturedAt;
  delete semantic.profileId;
  delete semantic.revision;
  delete semantic.options;
  delete semantic.nkroEnabled;
  // Capabilities describe the firmware, not a persisted setting. A newer
  // firmware may expose action programs after restoring a source profile that
  // predates them. Compare action data only when the source could actually read
  // and preserve that extension.
  delete semantic.capabilities;
  const sourceCapabilities = sourceSnapshot.capabilities ?? [];
  if (!sourceCapabilities.includes("action-programs-v1")) {
    delete semantic.actionPrograms;
    delete semantic.actionProgramNames;
  }
  if (!sourceCapabilities.includes("state-overlays-v1")) {
    delete semantic.actionOverlays;
    delete semantic.actionOverlayNames;
    delete semantic.actionStateBits;
  }
  return semantic;
}

function profilesEqual(
  left: FirmwareProfilesMigrationBackup,
  right: FirmwareProfilesMigrationBackup,
): boolean {
  if (
    left.usedMask !== right.usedMask
    || left.activeProfileIndex !== right.activeProfileIndex
    || left.defaultProfileIndex !== right.defaultProfileIndex
    || JSON.stringify(left.globals) !== JSON.stringify(right.globals)
    || left.names.some((name, index) => name !== right.names[index])
  ) {
    return false;
  }
  return left.snapshots.every((snapshot, index) => {
    const other = right.snapshots[index];
    if (snapshot == null || other == null) return snapshot === other;
    return JSON.stringify(snapshotSemantics(snapshot, snapshot))
      === JSON.stringify(snapshotSemantics(other, snapshot));
  });
}

async function captureProfilesBackup(
  serialNumber: string,
  device: UpdaterMigrationDevice,
  profileApi: FirmwareProfileMigrationApi,
): Promise<FirmwareProfilesMigrationBackup> {
  const [active, defaultProfile] = await Promise.all([
    device.getActiveProfile(),
    device.getDefaultProfile(),
  ]);
  if (
    !active
    || !defaultProfile
    || active.profile_used_mask !== defaultProfile.profile_used_mask
  ) {
    throw new Error(
      "SETTINGS_BACKUP_REQUIRED: profile metadata could not be read consistently; updater migration was not started",
    );
  }

  const usedMask = active.profile_used_mask & ((1 << SETTINGS_PROFILE_COUNT) - 1);
  let globals: FirmwareProfilesMigrationBackup["globals"] | null = null;
  const names: Array<string | null> = Array(SETTINGS_PROFILE_COUNT).fill(null);
  const snapshots: Array<FirmwareProfileSnapshot | null> = Array(SETTINGS_PROFILE_COUNT).fill(null);
  for (let profileIndex = 0; profileIndex < SETTINGS_PROFILE_COUNT; profileIndex += 1) {
    if (!(usedMask & (1 << profileIndex))) continue;
    const name = await device.getProfileName(profileIndex);
    const snapshot = await profileApi.capture(profileIndex);
    const completeSnapshot = normalizeCompleteProfileSnapshot(snapshot, profileIndex, serialNumber);
    if (!name || name.profile_used_mask !== usedMask || !completeSnapshot) {
      throw new Error(
        `SETTINGS_BACKUP_REQUIRED: on-device profile ${profileIndex + 1} could not be captured completely; updater migration was not started`,
      );
    }
    const snapshotGlobals: FirmwareProfilesMigrationBackup["globals"] = {
      options: completeSnapshot.options!,
      nkroEnabled: completeSnapshot.nkroEnabled!,
    };
    if (globals && JSON.stringify(globals) !== JSON.stringify(snapshotGlobals)) {
      throw new Error(
        "SETTINGS_BACKUP_REQUIRED: global options changed while profiles were captured; updater migration was not started",
      );
    }
    globals = snapshotGlobals;
    names[profileIndex] = name.name;
    snapshots[profileIndex] = completeSnapshot;
  }

  const captured = normalizeProfilesBackup({
    usedMask,
    activeProfileIndex: active.profile_index,
    defaultProfileIndex: defaultProfile.profile_index,
    globals,
    names,
    snapshots,
  }, serialNumber);
  const activeAfterCapture = await device.getActiveProfile();
  if (
    !captured
    || !activeAfterCapture
    || activeAfterCapture.profile_index !== captured.activeProfileIndex
    || activeAfterCapture.profile_used_mask !== captured.usedMask
  ) {
    throw new Error(
      "SETTINGS_BACKUP_REQUIRED: profile capture did not preserve active profile metadata; updater migration was not started",
    );
  }
  return captured;
}

async function restoreProfilesBackup(
  backup: LegacyProfilesMigrationBackup | CompleteUpdaterMigrationBackup,
  device: UpdaterMigrationDevice,
  profileApi: FirmwareProfileMigrationApi,
): Promise<void> {
  let active = await device.getActiveProfile();
  if (!active) {
    throw new Error("SETTINGS_RESTORE_REQUIRED: profile metadata is unavailable");
  }

  while (active.profile_used_mask !== (1 << SETTINGS_PROFILE_COUNT) - 1) {
    const before = active.profile_used_mask;
    const created = await device.createProfile();
    if (!created || created.profile_used_mask === before) {
      throw new Error("SETTINGS_RESTORE_REQUIRED: all profile slots could not be recreated");
    }
    active = created;
  }

  for (let profileIndex = 0; profileIndex < SETTINGS_PROFILE_COUNT; profileIndex += 1) {
    if (!(backup.profiles.usedMask & (1 << profileIndex))) continue;
    const snapshot = backup.profiles.snapshots[profileIndex];
    const name = backup.profiles.names[profileIndex];
    if (!snapshot || name == null || !(await device.resetProfileSlot(profileIndex))) {
      throw new Error(`SETTINGS_RESTORE_REQUIRED: profile ${profileIndex + 1} could not be reset`);
    }
    // Options/NKRO are global, so never let a per-profile apply decide their
    // final value. They are restored once from the explicitly captured globals.
    const applied = await profileApi.apply({
      ...snapshot,
      options: null,
      nkroEnabled: null,
    }, profileIndex, {
      persistToFlash: true,
      restoreActiveProfile: false,
    });
    if (!applied) {
      throw new Error(`SETTINGS_RESTORE_REQUIRED: profile ${profileIndex + 1} could not be restored`);
    }
    const renamed = await device.setProfileName(profileIndex, name);
    if (!renamed || renamed.name !== name) {
      throw new Error(`SETTINGS_RESTORE_REQUIRED: profile ${profileIndex + 1} name could not be restored`);
    }
  }

  if (!(await device.setActiveProfile(backup.profiles.activeProfileIndex))) {
    throw new Error("SETTINGS_RESTORE_REQUIRED: active profile could not be restored");
  }
  for (let profileIndex = 0; profileIndex < SETTINGS_PROFILE_COUNT; profileIndex += 1) {
    if (backup.profiles.usedMask & (1 << profileIndex)) continue;
    if (!(await device.deleteProfile(profileIndex))) {
      throw new Error(`SETTINGS_RESTORE_REQUIRED: unused profile slot ${profileIndex + 1} could not be removed`);
    }
  }
  if (!(await device.setDefaultProfile(backup.profiles.defaultProfileIndex))) {
    throw new Error("SETTINGS_RESTORE_REQUIRED: default profile could not be restored");
  }
  if (!(await device.setActiveProfile(backup.profiles.activeProfileIndex))) {
    throw new Error("SETTINGS_RESTORE_REQUIRED: active profile could not be finalized");
  }
  if (
    !(await device.setOptions(backup.profiles.globals.options))
    || !(await device.setNkroEnabled(backup.profiles.globals.nkroEnabled))
  ) {
    throw new Error("SETTINGS_RESTORE_REQUIRED: global keyboard options could not be restored");
  }
  if (!(await device.saveSettings())) {
    throw new Error("SETTINGS_RESTORE_REQUIRED: restored profiles could not be persisted");
  }
}

export async function getUpdaterMigrationBackup(
  serialNumber: string,
  store: CalibrationMigrationStore = persistentStore,
): Promise<UpdaterMigrationBackup | null> {
  const serial = normalizedSerialNumber(serialNumber);
  await store.init();
  const raw = await store.get(backupKey(serial));
  if (raw == null) return null;
  const backup = normalizeBackup(raw);
  if (!backup || backup.serialNumber !== serial) {
    throw new Error(
      "UPDATER_BACKUP_INVALID: the saved pre-migration recovery data is corrupt or belongs to another keyboard",
    );
  }
  return backup;
}

export async function listUpdaterMigrationBackups(
  store: CalibrationMigrationStore = persistentStore,
): Promise<UpdaterMigrationBackup[]> {
  await store.init();
  const backups: UpdaterMigrationBackup[] = [];
  for (const [key, raw] of await store.entries()) {
    if (!key.startsWith(STORE_KEY_PREFIX)) continue;
    const backup = normalizeBackup(raw);
    if (!backup || backupKey(backup.serialNumber) !== key) {
      throw new Error(
        "UPDATER_BACKUP_INVALID: saved pre-migration recovery data is corrupt",
      );
    }
    backups.push(backup);
  }
  return backups.sort((left, right) => right.capturedAt - left.capturedAt);
}

export async function captureUpdaterMigrationBackup(
  serialNumber: string,
  device: UpdaterMigrationDevice = kbheDevice,
  store: CalibrationMigrationStore = persistentStore,
  profileApi: FirmwareProfileMigrationApi = firmwareProfileApi,
): Promise<CompleteUpdaterMigrationBackup> {
  const serial = normalizedSerialNumber(serialNumber);
  const ramOnlyMode = await device.getRamOnlyMode();
  if (ramOnlyMode !== false) {
    throw new Error(
      "SETTINGS_BACKUP_REQUIRED: an app profile is active in RAM-only mode (or its state could not be verified). Return to an on-device profile before updater migration.",
    );
  }
  const keyboardName = normalizeKeyboardName(await device.getKeyboardName());
  if (!keyboardName) {
    throw new Error(
      "SETTINGS_BACKUP_REQUIRED: the persistent keyboard name could not be read exactly; updater migration was not started",
    );
  }
  const calibration = normalizeCalibrationSettings(await device.getCalibration());
  if (!calibration) {
    throw new Error(
      "CALIBRATION_BACKUP_REQUIRED: the keyboard's complete 82-key calibration could not be read; updater migration was not started",
    );
  }
  const profiles = await captureProfilesBackup(serial, device, profileApi);
  const keyboardNameAfterCapture = normalizeKeyboardName(await device.getKeyboardName());
  if (keyboardNameAfterCapture !== keyboardName) {
    throw new Error(
      "SETTINGS_BACKUP_REQUIRED: the keyboard name changed while settings were captured; updater migration was not started",
    );
  }

  const backup: CompleteUpdaterMigrationBackup = {
    schemaVersion: COMPLETE_SCHEMA_VERSION,
    serialNumber: serial,
    capturedAt: Date.now(),
    calibration,
    keyboardName,
    profiles,
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
      `SETTINGS_BACKUP_REQUIRED: complete pre-migration recovery data could not be saved; updater migration was not started (${error instanceof Error ? error.message : String(error)})`,
      { cause: error },
    );
  }

  const persisted = normalizeBackup(await store.get(key));
  if (
    !hasCompleteProfileRecovery(persisted)
    || persisted.serialNumber !== serial
    || persisted.keyboardName !== keyboardName
    || !calibrationsEqual(calibration, persisted.calibration)
    || !profilesEqual(profiles, persisted.profiles)
  ) {
    throw new Error(
      "SETTINGS_BACKUP_REQUIRED: complete keyboard-name/calibration/profile recovery data could not be persisted safely; updater migration was not started",
    );
  }
  return persisted;
}

export async function restoreUpdaterMigrationBackup(
  serialNumber: string,
  device: UpdaterMigrationDevice = kbheDevice,
  store: CalibrationMigrationStore = persistentStore,
  profileApi: FirmwareProfileMigrationApi = firmwareProfileApi,
): Promise<boolean> {
  const serial = normalizedSerialNumber(serialNumber);
  const backup = await getUpdaterMigrationBackup(serial, store);
  if (!backup) return false;

  if (hasProfileRecovery(backup)) {
    await restoreProfilesBackup(backup, device, profileApi);
  }

  if (hasCompleteProfileRecovery(backup)) {
    const restoredKeyboardName = await device.setKeyboardName(backup.keyboardName);
    if (restoredKeyboardName !== backup.keyboardName) {
      throw new Error(
        "SETTINGS_RESTORE_REQUIRED: the persistent keyboard name could not be restored; the backup was kept for retry",
      );
    }
  }

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
  if (!(await device.saveSettings())) {
    throw new Error(
      "SETTINGS_RESTORE_REQUIRED: calibration/profile recovery could not be persisted; the backup was kept for retry",
    );
  }

  const verified = normalizeCalibrationSettings(await device.getCalibration());
  if (!verified || !calibrationsEqual(backup.calibration, verified)) {
    throw new Error(
      "CALIBRATION_RESTORE_REQUIRED: calibration verification failed after the update; the backup was kept for retry",
    );
  }
  if (hasCompleteProfileRecovery(backup)) {
    const verifiedKeyboardName = normalizeKeyboardName(await device.getKeyboardName());
    if (verifiedKeyboardName !== backup.keyboardName) {
      throw new Error(
        "SETTINGS_RESTORE_REQUIRED: keyboard-name verification failed; the complete backup was kept for retry",
      );
    }
  }
  if (hasProfileRecovery(backup)) {
    const verifiedProfiles = await captureProfilesBackup(serial, device, profileApi);
    if (!profilesEqual(backup.profiles, verifiedProfiles)) {
      throw new Error(
        "SETTINGS_RESTORE_REQUIRED: semantic profile verification failed; the complete backup was kept for retry",
      );
    }
  }

  const key = backupKey(serial);
  await store.delete(key);
  try {
    await store.save();
  } catch (error) {
    // Do not let a failed acknowledgement make the recovery record disappear
    // from the running process. The prior durable copy is still authoritative;
    // restoring the in-memory value also keeps the banner/retry path visible.
    try {
      await store.set(key, backup);
    } catch {
      // The original durable-store failure remains the actionable error.
    }
    throw new Error(
      `SETTINGS_RESTORE_REQUIRED: every setting was restored and verified, but the recovery record could not be acknowledged; the backup was kept for retry (${error instanceof Error ? error.message : String(error)})`,
      { cause: error },
    );
  }
  return true;
}

export async function refreshUpdaterMigrationBackup(
  serialNumber: string,
  existingBackup: UpdaterMigrationBackup | null,
  device: UpdaterMigrationDevice = kbheDevice,
  store: CalibrationMigrationStore = persistentStore,
  profileApi: FirmwareProfileMigrationApi = firmwareProfileApi,
): Promise<CompleteUpdaterMigrationBackup> {
  if (existingBackup) {
    const restored = await restoreUpdaterMigrationBackup(
      serialNumber,
      device,
      store,
      profileApi,
    );
    if (!restored) {
      throw new Error(
        "SETTINGS_BACKUP_REQUIRED: the existing recovery snapshot disappeared before it could be restored and acknowledged",
      );
    }
  }
  return captureUpdaterMigrationBackup(serialNumber, device, store, profileApi);
}
