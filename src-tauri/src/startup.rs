use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;
use tauri::{AppHandle, Manager};

const STARTUP_PREFS_FILE: &str = "startup-preferences.json";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum StartupWindowMode {
    Normal,
    Tray,
    Fullscreen,
}

impl Default for StartupWindowMode {
    fn default() -> Self {
        Self::Normal
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct StartupPreferences {
    pub startup_mode: StartupWindowMode,
}

fn startup_prefs_path(app: &AppHandle) -> Result<PathBuf, String> {
    let base = app
        .path()
        .app_config_dir()
        .map_err(|error| error.to_string())?;

    fs::create_dir_all(&base).map_err(|error| error.to_string())?;
    Ok(base.join(STARTUP_PREFS_FILE))
}

fn save_startup_preferences(app: &AppHandle, prefs: &StartupPreferences) -> Result<(), String> {
    let path = startup_prefs_path(app)?;
    let bytes = serde_json::to_vec_pretty(prefs).map_err(|error| error.to_string())?;
    fs::write(path, bytes).map_err(|error| error.to_string())
}

pub fn load_startup_preferences_from_app_handle(app: &AppHandle) -> StartupPreferences {
    let path = match startup_prefs_path(app) {
        Ok(path) => path,
        Err(_) => return StartupPreferences::default(),
    };

    match fs::read(path) {
        Ok(bytes) => serde_json::from_slice::<StartupPreferences>(&bytes).unwrap_or_default(),
        Err(_) => StartupPreferences::default(),
    }
}

#[tauri::command]
pub fn kbhe_get_startup_preferences(app: AppHandle) -> Result<StartupPreferences, String> {
    Ok(load_startup_preferences_from_app_handle(&app))
}

#[tauri::command]
pub fn kbhe_set_startup_preferences(
    app: AppHandle,
    preferences: StartupPreferences,
) -> Result<(), String> {
    save_startup_preferences(&app, &preferences)
}
