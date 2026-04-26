import { invoke, isTauri } from "@tauri-apps/api/core";
import { disable, enable, isEnabled } from "@tauri-apps/plugin-autostart";
import { LEDEffect } from "@/lib/kbhe/protocol";

export type StartupWindowMode = "normal" | "tray" | "fullscreen";

export interface StartupPreferences {
  startupMode: StartupWindowMode;
}

export interface CloseLightingPreferences {
  enabled: boolean;
  closeEffect: number;
  restorePreviousOnStartup: boolean;
}

export interface CloseLightingRestoreSnapshot {
  effect: number;
  savedAt: number;
}

const WINDOWS_MICA_STORAGE_KEY = "kbhe-windows-mica-enabled";
const CLOSE_LIGHTING_PREFERENCES_STORAGE_KEY = "kbhe-close-lighting-preferences";
const CLOSE_LIGHTING_RESTORE_STORAGE_KEY = "kbhe-close-lighting-restore";
const WINDOWS_MICA_CLASS = "windows-mica";
const DEFAULT_WINDOWS_MICA_ENABLED = true;

const DEFAULT_STARTUP_PREFERENCES: StartupPreferences = {
  startupMode: "normal",
};

const DEFAULT_CLOSE_LIGHTING_PREFERENCES: CloseLightingPreferences = {
  enabled: false,
  closeEffect: LEDEffect.SOLID_COLOR,
  restorePreviousOnStartup: true,
};

export const STARTUP_WINDOW_MODE_OPTIONS: Array<{ value: StartupWindowMode; label: string }> = [
  { value: "normal", label: "Normal Window" },
  { value: "tray", label: "Start in Tray" },
  { value: "fullscreen", label: "Start Fullscreen" },
];

function sanitizeLedEffect(value: unknown): number {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return DEFAULT_CLOSE_LIGHTING_PREFERENCES.closeEffect;
  }

  return Math.max(0, Math.min(255, Math.trunc(numeric)));
}

function normalizeCloseLightingPreferences(value: unknown): CloseLightingPreferences {
  const parsed = typeof value === "object" && value !== null
    ? value as Partial<CloseLightingPreferences>
    : {};

  return {
    enabled: Boolean(parsed.enabled),
    closeEffect: sanitizeLedEffect(parsed.closeEffect),
    restorePreviousOnStartup: parsed.restorePreviousOnStartup == null
      ? DEFAULT_CLOSE_LIGHTING_PREFERENCES.restorePreviousOnStartup
      : Boolean(parsed.restorePreviousOnStartup),
  };
}

function normalizeCloseLightingRestoreSnapshot(value: unknown): CloseLightingRestoreSnapshot | null {
  const parsed = typeof value === "object" && value !== null
    ? value as Partial<CloseLightingRestoreSnapshot>
    : null;

  if (!parsed) {
    return null;
  }

  const effect = sanitizeLedEffect(parsed.effect);
  const savedAtRaw = Number(parsed.savedAt);
  const savedAt = Number.isFinite(savedAtRaw)
    ? Math.max(0, Math.trunc(savedAtRaw))
    : Date.now();

  return { effect, savedAt };
}

export async function getStartupPreferences(): Promise<StartupPreferences> {
  if (!isTauri()) {
    return DEFAULT_STARTUP_PREFERENCES;
  }

  try {
    const preferences = await invoke<StartupPreferences>("kbhe_get_startup_preferences");
    if (!preferences || !preferences.startupMode) {
      return DEFAULT_STARTUP_PREFERENCES;
    }
    return preferences;
  } catch {
    return DEFAULT_STARTUP_PREFERENCES;
  }
}

export async function setStartupPreferences(preferences: StartupPreferences): Promise<void> {
  if (!isTauri()) {
    return;
  }

  await invoke("kbhe_set_startup_preferences", { preferences });
}

export async function getLaunchOnStartupEnabled(): Promise<boolean> {
  if (!isTauri()) {
    return false;
  }

  try {
    return await isEnabled();
  } catch {
    return false;
  }
}

export async function setLaunchOnStartupEnabled(enabled: boolean): Promise<void> {
  if (!isTauri()) {
    return;
  }

  if (enabled) {
    await enable();
    return;
  }

  await disable();
}

export async function signalFrontendReady(): Promise<void> {
  if (!isTauri()) {
    return;
  }

  try {
    await invoke("kbhe_frontend_ready");
  } catch {
    // Best effort: splash fallback timer in Rust will still recover.
  }
}

function isWindowsPlatform(): boolean {
  const userAgentData = (navigator as Navigator & { userAgentData?: { platform?: string } }).userAgentData;
  const platform = userAgentData?.platform ?? navigator.platform ?? "";
  return /win/i.test(platform) || /windows/i.test(navigator.userAgent);
}

export function isWindowsMicaSupported(): boolean {
  return isTauri() && isWindowsPlatform();
}

export function getWindowsMicaEnabled(): boolean {
  if (!isWindowsMicaSupported()) {
    return false;
  }

  const stored = localStorage.getItem(WINDOWS_MICA_STORAGE_KEY);
  if (stored == null) {
    return DEFAULT_WINDOWS_MICA_ENABLED;
  }

  return stored === "true";
}

function applyWindowsMicaClass(enabled: boolean): void {
  document.documentElement.classList.toggle(WINDOWS_MICA_CLASS, isWindowsMicaSupported() && enabled);
}

export function setWindowsMicaEnabled(enabled: boolean): void {
  if (!isWindowsMicaSupported()) {
    return;
  }

  localStorage.setItem(WINDOWS_MICA_STORAGE_KEY, String(enabled));
  applyWindowsMicaClass(enabled);
}

export function applyWindowsMicaStyling(): void {
  applyWindowsMicaClass(getWindowsMicaEnabled());
}

export function getCloseLightingPreferences(): CloseLightingPreferences {
  const stored = localStorage.getItem(CLOSE_LIGHTING_PREFERENCES_STORAGE_KEY);
  if (!stored) {
    return DEFAULT_CLOSE_LIGHTING_PREFERENCES;
  }

  try {
    const parsed = JSON.parse(stored);
    return normalizeCloseLightingPreferences(parsed);
  } catch {
    return DEFAULT_CLOSE_LIGHTING_PREFERENCES;
  }
}

export function setCloseLightingPreferences(preferences: CloseLightingPreferences): void {
  const normalized = normalizeCloseLightingPreferences(preferences);
  localStorage.setItem(CLOSE_LIGHTING_PREFERENCES_STORAGE_KEY, JSON.stringify(normalized));
}

export function getCloseLightingRestoreSnapshot(): CloseLightingRestoreSnapshot | null {
  const stored = localStorage.getItem(CLOSE_LIGHTING_RESTORE_STORAGE_KEY);
  if (!stored) {
    return null;
  }

  try {
    const parsed = JSON.parse(stored);
    return normalizeCloseLightingRestoreSnapshot(parsed);
  } catch {
    return null;
  }
}

export function setCloseLightingRestoreSnapshot(effect: number): void {
  const snapshot: CloseLightingRestoreSnapshot = {
    effect: sanitizeLedEffect(effect),
    savedAt: Date.now(),
  };

  localStorage.setItem(CLOSE_LIGHTING_RESTORE_STORAGE_KEY, JSON.stringify(snapshot));
}

export function clearCloseLightingRestoreSnapshot(): void {
  localStorage.removeItem(CLOSE_LIGHTING_RESTORE_STORAGE_KEY);
}
