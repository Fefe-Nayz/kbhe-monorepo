mod commands;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
  tauri::Builder::default()
    .manage(commands::KbheTransportState::default())
    .plugin(tauri_plugin_store::Builder::default().build())
    .setup(|app| {
      if cfg!(debug_assertions) {
        app.handle().plugin(
          tauri_plugin_log::Builder::default()
            .level(log::LevelFilter::Info)
            .build(),
        )?;
      }
      Ok(())
    })
    .invoke_handler(tauri::generate_handler![
      commands::kbhe_list_devices,
      commands::kbhe_connect,
      commands::kbhe_disconnect,
      commands::kbhe_connection_state,
      commands::kbhe_flush_input,
      commands::kbhe_write_report,
      commands::kbhe_read_report,
      commands::kbhe_wait_for_device,
      commands::kbhe_wait_for_disconnect
    ])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}


