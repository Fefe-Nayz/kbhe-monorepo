mod audio;
mod commands;
mod releases;
mod startup;
mod volume;

use tauri::{
    image::Image,
    menu::{Menu, MenuBuilder, SubmenuBuilder},
    tray::TrayIconBuilder,
    AppHandle, Manager, WebviewWindow, WindowEvent,
};
use tauri_plugin_autostart::MacosLauncher;

const APP_DISPLAY_NAME: &str = "KBHE configurator";
const AUTOSTART_LAUNCH_FLAG: &str = "--kbhe-autostart";
const TRAY_ICON_SIZE: u32 = 16;
const TRAY_ID: &str = "main-tray";
const TRAY_ICON_OPEN_SVG: &str = include_str!("../../public/open.svg");
const TRAY_ICON_MINIMIZE_SVG: &str = include_str!("../../public/minimize.svg");
const TRAY_ICON_GOTO_SVG: &str = include_str!("../../public/goto.svg");
const TRAY_ICON_EXIT_SVG: &str = include_str!("../../public/exit.svg");

#[derive(Clone, Default)]
struct TrayMenuIcons {
    open: Option<Image<'static>>,
    minimize: Option<Image<'static>>,
    go_to: Option<Image<'static>>,
    exit: Option<Image<'static>>,
}

fn launched_from_autostart() -> bool {
    std::env::args_os().any(|arg| arg == AUTOSTART_LAUNCH_FLAG)
}

fn tray_icon_color(app: &AppHandle) -> &'static str {
    let dark_theme = app
        .get_webview_window("main")
        .and_then(|window| window.theme().ok())
        .is_some_and(|theme| matches!(theme, tauri::Theme::Dark));

    if dark_theme {
        "#F5F5F5"
    } else {
        "#242424"
    }
}

fn rasterize_svg_icon(svg_source: &str, color_hex: &str, size: u32) -> Option<Image<'static>> {
    let themed_svg = svg_source.replace("#242424", color_hex);
    let tree = resvg::usvg::Tree::from_str(&themed_svg, &resvg::usvg::Options::default()).ok()?;
    let mut pixmap = resvg::tiny_skia::Pixmap::new(size, size)?;

    let tree_size = tree.size();
    let scale_x = size as f32 / tree_size.width() as f32;
    let scale_y = size as f32 / tree_size.height() as f32;
    let scale = scale_x.min(scale_y);
    let offset_x = (size as f32 - tree_size.width() as f32 * scale) * 0.5;
    let offset_y = (size as f32 - tree_size.height() as f32 * scale) * 0.5;
    let transform = resvg::tiny_skia::Transform::from_scale(scale, scale)
        .post_translate(offset_x, offset_y);

    let _ = resvg::render(&tree, transform, &mut pixmap.as_mut());
    Some(Image::new_owned(pixmap.data().to_vec(), size, size))
}

fn build_tray_menu_icons(app: &AppHandle) -> TrayMenuIcons {
    let color = tray_icon_color(app);

    TrayMenuIcons {
        open: rasterize_svg_icon(TRAY_ICON_OPEN_SVG, color, TRAY_ICON_SIZE),
        minimize: rasterize_svg_icon(TRAY_ICON_MINIMIZE_SVG, color, TRAY_ICON_SIZE),
        go_to: rasterize_svg_icon(TRAY_ICON_GOTO_SVG, color, TRAY_ICON_SIZE),
        exit: rasterize_svg_icon(TRAY_ICON_EXIT_SVG, color, TRAY_ICON_SIZE),
    }
}

fn build_tray_menu(app: &AppHandle) -> tauri::Result<Menu<tauri::Wry>> {
    let icons = build_tray_menu_icons(app);

    let mut go_to_builder = SubmenuBuilder::with_id(app, "go_to", "Go to")
        .text("open_dashboard", "Dashboard")
        .text("open_lighting", "Lighting")
        .text("open_settings", "Settings");
    if let Some(icon) = icons.go_to.clone() {
        go_to_builder = go_to_builder.submenu_icon(icon);
    }
    let go_to_menu = go_to_builder.build()?;

    let mut menu_builder = MenuBuilder::new(app);
    if let Some(icon) = icons.open {
        menu_builder = menu_builder.icon("show", "Open", icon);
    } else {
        menu_builder = menu_builder.text("show", "Open");
    }

    if let Some(icon) = icons.minimize {
        menu_builder = menu_builder.icon("hide", "Hide", icon);
    } else {
        menu_builder = menu_builder.text("hide", "Hide");
    }

    menu_builder = menu_builder.separator().item(&go_to_menu).separator();

    if let Some(icon) = icons.exit {
        menu_builder = menu_builder.icon("quit", "Quit", icon);
    } else {
        menu_builder = menu_builder.text("quit", "Quit");
    }

    menu_builder.build()
}

fn show_main_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.show();
        let _ = window.set_focus();
    }
}

fn hide_main_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.hide();
    }
}

fn navigate_main_window(app: &AppHandle, route: &str) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.show();
        let _ = window.set_focus();

        let script = format!(
            "window.history.pushState(null, '', '{}'); window.dispatchEvent(new PopStateEvent('popstate'));",
            route
        );
        let _ = window.eval(&script);
    }
}

fn apply_startup_window_mode(window: &WebviewWindow, launched_from_autostart: bool) {
    if !launched_from_autostart {
        let _ = window.set_fullscreen(false);
        let _ = window.show();
        let _ = window.set_focus();
        return;
    }

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
        apply_startup_window_mode(&main, launched_from_autostart());
    }

    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(commands::KbheTransportState::default())
        .plugin(tauri_plugin_autostart::init(
            MacosLauncher::LaunchAgent,
            Some(vec![AUTOSTART_LAUNCH_FLAG]),
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

            let menu = build_tray_menu(&app.handle())?;

            let mut builder = TrayIconBuilder::with_id(TRAY_ID);
            if let Some(icon) = app.default_window_icon().cloned() {
                builder = builder.icon(icon);
            }
            builder
                .tooltip(APP_DISPLAY_NAME)
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        show_main_window(app);
                    }
                    "hide" => {
                        hide_main_window(app);
                    }
                    "open_dashboard" => {
                        navigate_main_window(app, "/");
                    }
                    "open_lighting" => {
                        navigate_main_window(app, "/lighting");
                    }
                    "open_settings" => {
                        navigate_main_window(app, "/settings");
                    }
                    "quit" => {
                        app.exit(0);
                    }
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if let tauri::tray::TrayIconEvent::DoubleClick { .. } = event {
                        show_main_window(&tray.app_handle());
                    }
                })
                .build(app)?;

            let app_handle = app.handle().clone();
            let startup_launch = launched_from_autostart();
            std::thread::spawn(move || {
                std::thread::sleep(std::time::Duration::from_secs(15));

                if let Some(splashscreen) = app_handle.get_webview_window("splashscreen") {
                    let _ = splashscreen.close();
                }

                if let Some(main) = app_handle.get_webview_window("main") {
                    apply_startup_window_mode(&main, startup_launch);
                }
            });

            Ok(())
        })
        .on_window_event(|window, event| {
            if window.label() != "main" {
                return;
            }

            if let WindowEvent::ThemeChanged(_) = event {
                if let Ok(menu) = build_tray_menu(&window.app_handle()) {
                    if let Some(tray) = window.app_handle().tray_by_id(TRAY_ID) {
                        let _ = tray.set_menu(Some(menu));
                    }
                }
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
            commands::kbhe_send_command,
            commands::kbhe_get_key_states,
            commands::kbhe_wait_for_device,
            commands::kbhe_wait_for_disconnect,
            commands::kbhe_detect_bootloader_presence,
            commands::kbhe_get_os_key_variants,
            commands::kbhe_flash_firmware,
            releases::kbhe_check_app_update,
            releases::kbhe_check_firmware_update,
            releases::kbhe_download_firmware_release,
            releases::kbhe_download_and_run_app_installer,
            kbhe_frontend_ready,
            startup::kbhe_get_startup_preferences,
            startup::kbhe_set_startup_preferences,
            volume::kbhe_get_system_volume,
            audio::kbhe_get_audio_bands,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
