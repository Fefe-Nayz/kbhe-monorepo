mod commands;
mod startup;
mod volume;

use tauri::{
    menu::{Menu, MenuItem},
    tray::TrayIconBuilder,
    AppHandle, Manager, WebviewWindow, WindowEvent,
};
use tauri_plugin_autostart::MacosLauncher;

fn apply_startup_window_mode(window: &WebviewWindow) {
    let prefs = startup::load_startup_preferences_from_app_handle(&window.app_handle());

    match prefs.startup_mode {
        startup::StartupWindowMode::Tray => {
            let _ = window.set_fullscreen(false);
            let _ = window.hide();
        }
        startup::StartupWindowMode::Fullscreen => {
            let _ = window.show();
            let _ = window.set_fullscreen(true);
            let _ = window.set_focus();
        }
        startup::StartupWindowMode::Normal => {
            let _ = window.set_fullscreen(false);
            let _ = window.show();
            let _ = window.set_focus();
        }
    }
}

#[tauri::command]
fn kbhe_frontend_ready(app: AppHandle) -> Result<(), String> {
    if let Some(splashscreen) = app.get_webview_window("splashscreen") {
        let _ = splashscreen.close();
    }

    if let Some(main) = app.get_webview_window("main") {
        apply_startup_window_mode(&main);
    }

    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(commands::KbheTransportState::default())
        .plugin(tauri_plugin_autostart::init(
            MacosLauncher::LaunchAgent,
            None::<Vec<&'static str>>,
        ))
        .plugin(tauri_plugin_store::Builder::default().build())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .setup(|app| {
            if cfg!(debug_assertions) {
                app.handle().plugin(
                    tauri_plugin_log::Builder::default()
                        .level(log::LevelFilter::Info)
                        .build(),
                )?;
            }

            let show = MenuItem::with_id(app, "show", "Show", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show, &quit])?;

            let mut builder = TrayIconBuilder::new();
            if let Some(icon) = app.default_window_icon().cloned() {
                builder = builder.icon(icon);
            }
            builder
                .tooltip("KBHE Configurator")
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                    "quit" => {
                        app.exit(0);
                    }
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if let tauri::tray::TrayIconEvent::DoubleClick { .. } = event {
                        if let Some(w) = tray.app_handle().get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                })
                .build(app)?;

            let app_handle = app.handle().clone();
            std::thread::spawn(move || {
                std::thread::sleep(std::time::Duration::from_secs(15));

                if let Some(splashscreen) = app_handle.get_webview_window("splashscreen") {
                    let _ = splashscreen.close();
                }

                if let Some(main) = app_handle.get_webview_window("main") {
                    apply_startup_window_mode(&main);
                }
            });

            Ok(())
        })
        .on_window_event(|window, event| {
            if window.label() != "main" {
                return;
            }

            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::kbhe_list_devices,
            commands::kbhe_connect,
            commands::kbhe_disconnect,
            commands::kbhe_connection_state,
            commands::kbhe_flush_input,
            commands::kbhe_write_report,
            commands::kbhe_read_report,
            commands::kbhe_get_key_states,
            commands::kbhe_wait_for_device,
            commands::kbhe_wait_for_disconnect,
            commands::kbhe_get_os_key_variants,
            kbhe_frontend_ready,
            startup::kbhe_get_startup_preferences,
            startup::kbhe_set_startup_preferences,
            volume::kbhe_get_system_volume,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
