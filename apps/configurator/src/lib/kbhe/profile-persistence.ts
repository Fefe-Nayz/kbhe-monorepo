import { isTauri } from "@tauri-apps/api/core";
import { LazyStore } from "@tauri-apps/plugin-store";

const durableStore = new LazyStore("profiles-v2.json", { autoSave: 250, defaults: {} });
const WAL_PREFIX = "kbhe-profile-wal:";
const memory = new Map<string, string>();
const pendingWrites = new Map<string, string | null>();
const errorListeners = new Set<(message: string | null) => void>();
let initializePromise: Promise<void> | null = null;
let durableReady = false;
let writeTail: Promise<void> = Promise.resolve();

function isProfileKey(key: string): boolean {
  return key.startsWith("keyboard-profile:")
    || key.startsWith("keyboard-device-profile:")
    || key === "keyboard-active-app-profile"
    || key === "keyboard-active-profile";
}

function reportError(error: unknown): void {
  const message = error instanceof Error ? error.message : String(error);
  for (const listener of errorListeners) listener(message);
}

function clearError(): void {
  for (const listener of errorListeners) listener(null);
}

function localGet(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch (error) {
    reportError(error);
    return null;
  }
}

function localSet(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch (error) {
    reportError(error);
  }
}

function localRemove(key: string): void {
  try {
    localStorage.removeItem(key);
  } catch (error) {
    reportError(error);
  }
}

function localKeys(): string[] {
  try {
    return Object.keys(localStorage).filter(isProfileKey);
  } catch (error) {
    reportError(error);
    return [];
  }
}

function allLocalKeys(): string[] {
  try {
    return Object.keys(localStorage);
  } catch (error) {
    reportError(error);
    return [];
  }
}

interface ProfileWalRecord {
  key: string;
  value: string | null;
}

function walStorageKey(key: string): string {
  return `${WAL_PREFIX}${encodeURIComponent(key)}`;
}

export function encodeProfileWalRecord(key: string, value: string | null): string {
  return JSON.stringify({ key, value } satisfies ProfileWalRecord);
}

export function decodeProfileWalRecord(raw: string): ProfileWalRecord | null {
  try {
    const parsed = JSON.parse(raw) as Partial<ProfileWalRecord>;
    if (
      typeof parsed.key !== "string"
      || !isProfileKey(parsed.key)
      || (parsed.value !== null && typeof parsed.value !== "string")
    ) {
      return null;
    }
    return { key: parsed.key, value: parsed.value };
  } catch {
    return null;
  }
}

function writeRecoveryRecord(key: string, value: string | null): void {
  localSet(walStorageKey(key), encodeProfileWalRecord(key, value));
}

function readRecoveryRecords(): Array<{ storageKey: string; record: ProfileWalRecord }> {
  const records: Array<{ storageKey: string; record: ProfileWalRecord }> = [];
  for (const storageKey of allLocalKeys()) {
    if (!storageKey.startsWith(WAL_PREFIX)) continue;
    const raw = localGet(storageKey);
    const record = raw == null ? null : decodeProfileWalRecord(raw);
    if (!record || walStorageKey(record.key) !== storageKey) {
      reportError(`invalid profile recovery record: ${storageKey}`);
      continue;
    }
    records.push({ storageKey, record });
  }
  return records;
}

function clearRecoveryRecord(key: string, persistedValue: string | null): void {
  const storageKey = walStorageKey(key);
  const raw = localGet(storageKey);
  const record = raw == null ? null : decodeProfileWalRecord(raw);
  if (record?.key === key && record.value === persistedValue) {
    localRemove(storageKey);
  }
}

function enqueueDurableWrite(key: string, value: string | null): void {
  pendingWrites.set(key, value);
  writeTail = writeTail.then(async () => {
    await initializeProfilePersistence();
    if (!durableReady) return;
    const latest = pendingWrites.get(key);
    if (latest === undefined) return;
    if (latest === null) await durableStore.delete(key);
    else await durableStore.set(key, latest);
    // Store.set/delete update the in-memory store; save is the durability
    // boundary. Never discard the WAL before the file has been flushed.
    await durableStore.save();
    // A newer write may have arrived while the async store call was running.
    // Only acknowledge/remove the local recovery copy we actually persisted.
    if (pendingWrites.get(key) === latest) {
      pendingWrites.delete(key);
      clearRecoveryRecord(key, latest);
      localRemove(key);
    }
    clearError();
  }).catch(reportError);
}

export function subscribeProfilePersistenceErrors(
  listener: (message: string | null) => void,
): () => void {
  errorListeners.add(listener);
  return () => errorListeners.delete(listener);
}

export async function initializeProfilePersistence(): Promise<void> {
  if (initializePromise) return initializePromise;
  initializePromise = (async () => {
    if (!isTauri()) return;
    const recoveries = readRecoveryRecords();
    for (const { record } of recoveries) {
      if (pendingWrites.has(record.key)) continue;
      if (record.value === null) memory.delete(record.key);
      else memory.set(record.key, record.value);
    }
    await durableStore.init();
    const entries = await durableStore.entries<string>();
    for (const [key, value] of entries) {
      if (
        typeof value === "string"
        && !pendingWrites.has(key)
        && !recoveries.some(({ record }) => record.key === key)
      ) {
        memory.set(key, value);
      }
    }
    let migrated = false;
    const recoveryKeysToClear: Array<{ storageKey: string; profileKey: string }> = [];
    for (const { storageKey, record } of recoveries) {
      if (pendingWrites.has(record.key)) continue;
      if (record.value === null) {
        await durableStore.delete(record.key);
        memory.delete(record.key);
      } else {
        await durableStore.set(record.key, record.value);
        memory.set(record.key, record.value);
      }
      migrated = true;
      recoveryKeysToClear.push({ storageKey, profileKey: record.key });
    }
    const legacyKeysToClear: string[] = [];
    for (const key of localKeys()) {
      if (!memory.has(key)) {
        const value = localGet(key);
        if (value != null) {
          memory.set(key, value);
          await durableStore.set(key, value);
          migrated = true;
        }
      }
      // Direct profile keys are legacy migration input, not a durable write
      // acknowledgement. New crash recovery records live under WAL_PREFIX.
      legacyKeysToClear.push(key);
    }
    if (migrated) {
      await durableStore.save();
    }
    for (const { storageKey, profileKey } of recoveryKeysToClear) {
      localRemove(storageKey);
      localRemove(profileKey);
    }
    for (const key of legacyKeysToClear) {
      localRemove(key);
    }
    durableReady = true;
    // Writes whose earlier initialization attempt failed still have a WAL but
    // no queued task. Re-enqueue every outstanding key after recovery.
    for (const [key, value] of pendingWrites) {
      enqueueDurableWrite(key, value);
    }
    clearError();
  })().catch((error) => {
    durableReady = false;
    initializePromise = null;
    reportError(error);
  });
  return initializePromise;
}

export function getProfileStorageKeys(): string[] {
  return Array.from(new Set([...localKeys(), ...memory.keys()]));
}

export function getProfileStorageItem(key: string): string | null {
  if (memory.has(key)) return memory.get(key) ?? null;
  const value = localGet(key);
  if (value != null) memory.set(key, value);
  return value;
}

export function setProfileStorageItem(key: string, value: string): void {
  memory.set(key, value);
  if (isTauri()) {
    /* Publish the recovery record before removing a legacy direct value. A
     * crash between those two synchronous operations must never reveal the
     * older durable profile without the newer write-ahead record. */
    writeRecoveryRecord(key, value);
    localRemove(key);
    enqueueDurableWrite(key, value);
  } else {
    localSet(key, value);
  }
}

export function removeProfileStorageItem(key: string): void {
  memory.delete(key);
  if (isTauri()) {
    // A deletion needs a durable tombstone too; absence from localStorage is
    // otherwise indistinguishable from a failed/never-issued store delete.
    writeRecoveryRecord(key, null);
    localRemove(key);
    enqueueDurableWrite(key, null);
  } else {
    localRemove(key);
  }
}
