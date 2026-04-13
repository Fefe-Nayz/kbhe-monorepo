import { invoke, isTauri } from "@tauri-apps/api/core";
import { disable, enable, isEnabled } from "@tauri-apps/plugin-autostart";

export type StartupWindowMode = "normal" | "tray" | "fullscreen";

export interface StartupPreferences {
  startupMode: StartupWindowMode;
}

const DEFAULT_STARTUP_PREFERENCES: StartupPreferences = {
  startupMode: "normal",
};

export const STARTUP_WINDOW_MODE_OPTIONS: Array<{ value: StartupWindowMode; label: string }> = [
  { value: "normal", label: "Normal Window" },
  { value: "tray", label: "Start in Tray" },
  { value: "fullscreen", label: "Start Fullscreen" },
];

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
