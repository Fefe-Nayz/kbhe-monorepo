use hidapi::{DeviceInfo, HidApi, HidDevice};
use serde::Serialize;
use std::collections::HashMap;
use std::ffi::CString;
use std::sync::{Mutex, MutexGuard};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, State};

const KBHE_VID: u16 = 0x9172;
const KBHE_APP_PID: u16 = 0x0002;
const KBHE_UPDATER_PID: u16 = 0x0003;
const KBHE_RAW_HID_USAGE_PAGE: u16 = 0xFF00;
const KBHE_APP_RAW_HID_INTERFACE: i32 = 1;
const KBHE_PACKET_SIZE: usize = 64;
const KBHE_KEY_COUNT: usize = 82;
const KBHE_KEY_STATES_PER_CHUNK: usize = 15;
const CMD_GET_KEY_STATES: u8 = 0xE1;
const STATUS_OK: u8 = 0x00;

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
enum KbheDeviceKind {
    Runtime,
    Updater,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct KbheHidDeviceInfo {
    path: String,
    vid: u16,
    pid: u16,
    kind: KbheDeviceKind,
    interface_number: Option<i32>,
    usage_page: Option<u16>,
    usage: Option<u16>,
    manufacturer: Option<String>,
    product: Option<String>,
    serial_number: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct KbheConnectionState {
    connected: bool,
    path: Option<String>,
    pid: Option<u16>,
    kind: Option<KbheDeviceKind>,
}

#[derive(Debug, Clone, Serialize)]
pub struct KbheKeyStatesSnapshot {
    states: Vec<u8>,
    distances: Vec<u8>,
    distances_01mm: Vec<u16>,
    distances_mm: Vec<f32>,
}

struct ActiveConnection {
    device: HidDevice,
    path: String,
    pid: u16,
    kind: KbheDeviceKind,
}

#[derive(Default)]
pub struct KbheTransportState {
    active: Mutex<Option<ActiveConnection>>,
}

fn lock_active<'a>(
    state: &'a State<'_, KbheTransportState>,
) -> Result<MutexGuard<'a, Option<ActiveConnection>>, String> {
    state
        .active
        .lock()
        .map_err(|_| "hid transport state is poisoned".to_string())
}

fn path_to_string(info: &DeviceInfo) -> String {
    info.path().to_string_lossy().into_owned()
}

fn optional_interface_number(info: &DeviceInfo) -> Option<i32> {
    let value = info.interface_number();
    (value >= 0).then_some(value)
}

fn optional_usage_page(info: &DeviceInfo) -> Option<u16> {
    let value = info.usage_page();
    (value != 0).then_some(value)
}

fn optional_usage(info: &DeviceInfo) -> Option<u16> {
    let value = info.usage();
    (value != 0).then_some(value)
}

fn matches_runtime(info: &DeviceInfo) -> bool {
    info.vendor_id() == KBHE_VID
        && info.product_id() == KBHE_APP_PID
        && (info.interface_number() == KBHE_APP_RAW_HID_INTERFACE
            || info.usage_page() == KBHE_RAW_HID_USAGE_PAGE)
}

fn matches_updater(info: &DeviceInfo) -> bool {
    info.vendor_id() == KBHE_VID
        && info.product_id() == KBHE_UPDATER_PID
        && info.usage_page() == KBHE_RAW_HID_USAGE_PAGE
}

fn classify_device(info: &DeviceInfo) -> Option<KbheDeviceKind> {
    if matches_runtime(info) {
        Some(KbheDeviceKind::Runtime)
    } else if matches_updater(info) {
        Some(KbheDeviceKind::Updater)
    } else {
        None
    }
}

fn is_bootloader_candidate(info: &DeviceInfo) -> bool {
    if info.vendor_id() != KBHE_VID {
        return false;
    }

    if info.product_id() == KBHE_UPDATER_PID {
        return true;
    }

    let product = info
        .product_string()
        .map(|value| value.to_ascii_lowercase())
        .unwrap_or_default();
    let manufacturer = info
        .manufacturer_string()
        .map(|value| value.to_ascii_lowercase())
        .unwrap_or_default();

    let looks_like_update_mode = product.contains("bootloader")
        || product.contains("updater")
        || product.contains("dfu");

    looks_like_update_mode && (manufacturer.contains("kbhe") || manufacturer.contains("keyboard"))
}

fn device_info_from(info: &DeviceInfo) -> Option<KbheHidDeviceInfo> {
    let kind = classify_device(info)?;
    Some(KbheHidDeviceInfo {
        path: path_to_string(info),
        vid: info.vendor_id(),
        pid: info.product_id(),
        kind,
        interface_number: optional_interface_number(info),
        usage_page: optional_usage_page(info),
        usage: optional_usage(info),
        manufacturer: info.manufacturer_string().map(|value| value.to_string()),
        product: info.product_string().map(|value| value.to_string()),
        serial_number: info.serial_number().map(|value| value.to_string()),
    })
}

fn enumerate_kbhe_devices() -> Result<Vec<KbheHidDeviceInfo>, String> {
    let api = HidApi::new().map_err(|error| error.to_string())?;
    let mut devices = Vec::new();

    for info in api.device_list() {
        if let Some(device) = device_info_from(info) {
            devices.push(device);
        }
    }

    Ok(devices)
}

fn parse_kind(kind: &str) -> Result<KbheDeviceKind, String> {
    match kind.trim().to_ascii_lowercase().as_str() {
        "runtime" | "app" => Ok(KbheDeviceKind::Runtime),
        "updater" | "bootloader" => Ok(KbheDeviceKind::Updater),
        _ => Err(format!("unknown KBHE device kind: {kind}")),
    }
}

fn find_first_device(kind: KbheDeviceKind) -> Result<Option<KbheHidDeviceInfo>, String> {
    let devices = enumerate_kbhe_devices()?;
    Ok(devices.into_iter().find(|device| device.kind == kind))
}

fn open_device_by_path(path: &str) -> Result<HidDevice, String> {
    let api = HidApi::new().map_err(|error| error.to_string())?;
    let path = CString::new(path).map_err(|_| "hid path contains NUL byte".to_string())?;
    api.open_path(&path).map_err(|error| error.to_string())
}

fn active_connection_state(active: Option<&ActiveConnection>) -> KbheConnectionState {
    KbheConnectionState {
        connected: active.is_some(),
        path: active.map(|connection| connection.path.clone()),
        pid: active.map(|connection| connection.pid),
        kind: active.map(|connection| connection.kind),
    }
}

fn send_command_on_active(
    connection: &mut ActiveConnection,
    command: u8,
    data: &[u8],
    timeout_ms: u64,
) -> Result<Option<Vec<u8>>, String> {
    let mut flush_buffer = [0u8; KBHE_PACKET_SIZE];
    loop {
        let read = connection
            .device
            .read_timeout(&mut flush_buffer, 0)
            .map_err(|error| error.to_string())?;
        if read == 0 {
            break;
        }
    }

    let mut report = vec![0u8; KBHE_PACKET_SIZE + 1];
    report[0] = 0;
    report[1] = command;
    for (index, byte) in data.iter().enumerate() {
        let offset = index + 2;
        if offset >= report.len() {
            break;
        }
        report[offset] = *byte;
    }

    connection
        .device
        .write(&report)
        .map_err(|error| error.to_string())?;

    let deadline = Instant::now() + Duration::from_millis(timeout_ms);
    let mut read_buffer = [0u8; KBHE_PACKET_SIZE];

    while Instant::now() < deadline {
        let remaining = deadline.saturating_duration_since(Instant::now());
        let timeout = i32::try_from(remaining.as_millis().max(1)).unwrap_or(i32::MAX);
        let read = connection
            .device
            .read_timeout(&mut read_buffer, timeout)
            .map_err(|error| error.to_string())?;

        if read == 0 {
            continue;
        }

        let response = read_buffer[..read].to_vec();
        if response.len() >= 2 && response[0] == command {
            return Ok(Some(response));
        }
    }

    Ok(None)
}

#[tauri::command]
pub fn kbhe_list_devices() -> Result<Vec<KbheHidDeviceInfo>, String> {
    enumerate_kbhe_devices()
}

#[tauri::command]
pub fn kbhe_detect_bootloader_presence() -> Result<bool, String> {
    let api = HidApi::new().map_err(|error| error.to_string())?;

    let mut present = false;
    for info in api.device_list() {
        if is_bootloader_candidate(info) {
            present = true;
            break;
        }
    }

    Ok(present)
}

#[tauri::command]
pub fn kbhe_connect(
    path: String,
    state: State<'_, KbheTransportState>,
) -> Result<KbheConnectionState, String> {
    let devices = enumerate_kbhe_devices()?;
    let matched = devices
        .into_iter()
        .find(|device| device.path == path)
        .ok_or_else(|| format!("KBHE device path not found: {path}"))?;

    let device = open_device_by_path(&matched.path)?;
    let mut active = lock_active(&state)?;
    *active = Some(ActiveConnection {
        device,
        path: matched.path.clone(),
        pid: matched.pid,
        kind: matched.kind,
    });

    Ok(active_connection_state(active.as_ref()))
}

#[tauri::command]
pub fn kbhe_disconnect(state: State<'_, KbheTransportState>) -> Result<KbheConnectionState, String> {
    let mut active = lock_active(&state)?;
    *active = None;
    Ok(active_connection_state(None))
}

#[tauri::command]
pub fn kbhe_connection_state(
    state: State<'_, KbheTransportState>,
) -> Result<KbheConnectionState, String> {
    let active = lock_active(&state)?;
    Ok(active_connection_state(active.as_ref()))
}

#[tauri::command]
pub fn kbhe_flush_input(state: State<'_, KbheTransportState>) -> Result<usize, String> {
    let mut active = lock_active(&state)?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    let mut flushed = 0usize;
    let mut buffer = [0u8; KBHE_PACKET_SIZE];

    loop {
        let read = connection
            .device
            .read_timeout(&mut buffer, 0)
            .map_err(|error| error.to_string())?;
        if read == 0 {
            break;
        }
        flushed += 1;
    }

    Ok(flushed)
}

#[tauri::command]
pub fn kbhe_write_report(
    report: Vec<u8>,
    state: State<'_, KbheTransportState>,
) -> Result<usize, String> {
    let mut active = lock_active(&state)?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    connection
        .device
        .write(&report)
        .map_err(|error| error.to_string())
}

#[tauri::command]
pub fn kbhe_read_report(
    timeout_ms: u64,
    state: State<'_, KbheTransportState>,
) -> Result<Vec<u8>, String> {
    let mut active = lock_active(&state)?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    let timeout_ms = i32::try_from(timeout_ms).unwrap_or(i32::MAX);
    let mut buffer = [0u8; KBHE_PACKET_SIZE];
    let read = connection
        .device
        .read_timeout(&mut buffer, timeout_ms)
        .map_err(|error| error.to_string())?;

    Ok(buffer[..read].to_vec())
}

#[tauri::command]
pub fn kbhe_send_command(
    command: u8,
    data: Vec<u8>,
    timeout_ms: u64,
    state: State<'_, KbheTransportState>,
) -> Result<Option<Vec<u8>>, String> {
    let mut active = lock_active(&state)?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    send_command_on_active(connection, command, &data, timeout_ms)
}

#[tauri::command]
pub fn kbhe_get_key_states(
    state: State<'_, KbheTransportState>,
) -> Result<KbheKeyStatesSnapshot, String> {
    let mut active = lock_active(&state)?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    let mut states = vec![0u8; KBHE_KEY_COUNT];
    let mut distances = vec![0u8; KBHE_KEY_COUNT];
    let mut distances_01mm = vec![0u16; KBHE_KEY_COUNT];
    let mut distances_mm = vec![0f32; KBHE_KEY_COUNT];

    let mut next_index = 0usize;
    while next_index < KBHE_KEY_COUNT {
        let response = send_command_on_active(
            connection,
            CMD_GET_KEY_STATES,
            &[0, next_index as u8],
            150,
        )?
        .ok_or_else(|| "timeout waiting for GET_KEY_STATES response".to_string())?;

        if response.len() < 4 || response[1] != STATUS_OK {
            return Err("invalid GET_KEY_STATES response header".to_string());
        }

        let start_index = usize::from(response[2]);
        let key_count = usize::from(response[3]);

        if start_index != next_index || key_count == 0 || key_count > KBHE_KEY_STATES_PER_CHUNK {
            return Err("invalid GET_KEY_STATES chunk metadata".to_string());
        }

        let expected_len = 4 + key_count * 4;
        if response.len() < expected_len {
            return Err("truncated GET_KEY_STATES chunk payload".to_string());
        }

        for index in 0..key_count {
            let offset = 4 + index * 4;
            let key_index = start_index + index;
            let state_value = response[offset];
            let distance_value = response[offset + 1];
            let value_01mm = u16::from(response[offset + 2]) | (u16::from(response[offset + 3]) << 8);

            states[key_index] = state_value;
            distances[key_index] = distance_value;
            distances_01mm[key_index] = value_01mm;
            distances_mm[key_index] = f32::from(value_01mm) / 100.0;
        }

        next_index += key_count;
    }

    Ok(KbheKeyStatesSnapshot {
        states,
        distances,
        distances_01mm,
        distances_mm,
    })
}

#[tauri::command]
pub fn kbhe_wait_for_device(
    kind: String,
    timeout_ms: u64,
) -> Result<Option<KbheHidDeviceInfo>, String> {
    let expected = parse_kind(&kind)?;
    let deadline = Instant::now() + Duration::from_millis(timeout_ms);

    loop {
        if let Some(device) = find_first_device(expected)? {
            return Ok(Some(device));
        }

        if Instant::now() >= deadline {
            return Ok(None);
        }

        std::thread::sleep(Duration::from_millis(100));
    }
}

#[tauri::command]
pub fn kbhe_wait_for_disconnect(kind: String, timeout_ms: u64) -> Result<bool, String> {
    let expected = parse_kind(&kind)?;
    let deadline = Instant::now() + Duration::from_millis(timeout_ms);

    loop {
        if find_first_device(expected)?.is_none() {
            return Ok(true);
        }

        if Instant::now() >= deadline {
            return Ok(false);
        }

        std::thread::sleep(Duration::from_millis(50));
    }
}

#[derive(Debug, Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct KbheOsKeyVariants {
    base: Option<String>,
    shift: Option<String>,
    alt_gr: Option<String>,
    shift_alt_gr: Option<String>,
}

#[cfg(target_os = "windows")]
#[derive(Clone, Copy)]
struct KeyProbe {
    code: &'static str,
    scan_code: u32,
}

#[cfg(target_os = "windows")]
const KEY_PROBES: &[KeyProbe] = &[
    KeyProbe { code: "Backquote", scan_code: 0x29 },
    KeyProbe { code: "Digit1", scan_code: 0x02 },
    KeyProbe { code: "Digit2", scan_code: 0x03 },
    KeyProbe { code: "Digit3", scan_code: 0x04 },
    KeyProbe { code: "Digit4", scan_code: 0x05 },
    KeyProbe { code: "Digit5", scan_code: 0x06 },
    KeyProbe { code: "Digit6", scan_code: 0x07 },
    KeyProbe { code: "Digit7", scan_code: 0x08 },
    KeyProbe { code: "Digit8", scan_code: 0x09 },
    KeyProbe { code: "Digit9", scan_code: 0x0A },
    KeyProbe { code: "Digit0", scan_code: 0x0B },
    KeyProbe { code: "Minus", scan_code: 0x0C },
    KeyProbe { code: "Equal", scan_code: 0x0D },
    KeyProbe { code: "KeyQ", scan_code: 0x10 },
    KeyProbe { code: "KeyW", scan_code: 0x11 },
    KeyProbe { code: "KeyE", scan_code: 0x12 },
    KeyProbe { code: "KeyR", scan_code: 0x13 },
    KeyProbe { code: "KeyT", scan_code: 0x14 },
    KeyProbe { code: "KeyY", scan_code: 0x15 },
    KeyProbe { code: "KeyU", scan_code: 0x16 },
    KeyProbe { code: "KeyI", scan_code: 0x17 },
    KeyProbe { code: "KeyO", scan_code: 0x18 },
    KeyProbe { code: "KeyP", scan_code: 0x19 },
    KeyProbe { code: "BracketLeft", scan_code: 0x1A },
    KeyProbe { code: "BracketRight", scan_code: 0x1B },
    KeyProbe { code: "IntlHash", scan_code: 0x2B },
    KeyProbe { code: "Backslash", scan_code: 0x2B },
    KeyProbe { code: "KeyA", scan_code: 0x1E },
    KeyProbe { code: "KeyS", scan_code: 0x1F },
    KeyProbe { code: "KeyD", scan_code: 0x20 },
    KeyProbe { code: "KeyF", scan_code: 0x21 },
    KeyProbe { code: "KeyG", scan_code: 0x22 },
    KeyProbe { code: "KeyH", scan_code: 0x23 },
    KeyProbe { code: "KeyJ", scan_code: 0x24 },
    KeyProbe { code: "KeyK", scan_code: 0x25 },
    KeyProbe { code: "KeyL", scan_code: 0x26 },
    KeyProbe { code: "Semicolon", scan_code: 0x27 },
    KeyProbe { code: "Quote", scan_code: 0x28 },
    KeyProbe { code: "IntlBackslash", scan_code: 0x56 },
    KeyProbe { code: "KeyZ", scan_code: 0x2C },
    KeyProbe { code: "KeyX", scan_code: 0x2D },
    KeyProbe { code: "KeyC", scan_code: 0x2E },
    KeyProbe { code: "KeyV", scan_code: 0x2F },
    KeyProbe { code: "KeyB", scan_code: 0x30 },
    KeyProbe { code: "KeyN", scan_code: 0x31 },
    KeyProbe { code: "KeyM", scan_code: 0x32 },
    KeyProbe { code: "Comma", scan_code: 0x33 },
    KeyProbe { code: "Period", scan_code: 0x34 },
    KeyProbe { code: "Slash", scan_code: 0x35 },
];

#[cfg(target_os = "windows")]
fn normalize_variant_value(value: Option<String>) -> Option<String> {
    let value = value?;
    let trimmed = value.trim().to_string();
    if trimmed.is_empty() {
        return None;
    }
    Some(trimmed)
}

#[cfg(target_os = "windows")]
fn extract_variant_for_state(
    hkl: windows::Win32::UI::Input::KeyboardAndMouse::HKL,
    vk: u32,
    scan_code: u32,
    key_state: &[u8; 256],
) -> Option<String> {
    use windows::Win32::UI::Input::KeyboardAndMouse::ToUnicodeEx;

    let mut buffer = [0u16; 8];

    let len = unsafe { ToUnicodeEx(vk, scan_code, key_state, &mut buffer, 0, Some(hkl)) };

    if len == 0 {
        return None;
    }

    let take = len.unsigned_abs() as usize;
    let value = String::from_utf16_lossy(&buffer[..take]);

    if len < 0 {
        let empty_state = [0u8; 256];
        for _ in 0..8 {
            let mut flush_buffer = [0u16; 8];
            let flush = unsafe { ToUnicodeEx(vk, scan_code, &empty_state, &mut flush_buffer, 0, Some(hkl)) };
            if flush >= 0 {
                break;
            }
        }
    }

    normalize_variant_value(Some(value))
}

#[cfg(target_os = "windows")]
fn get_os_key_variants_impl() -> Result<HashMap<String, KbheOsKeyVariants>, String> {
    use windows::Win32::UI::Input::KeyboardAndMouse::{
        GetKeyboardLayout, MapVirtualKeyExW, MAPVK_VSC_TO_VK_EX, VK_CONTROL, VK_MENU, VK_RCONTROL,
        VK_RMENU, VK_RSHIFT, VK_SHIFT,
    };
    use windows::Win32::UI::WindowsAndMessaging::{GetForegroundWindow, GetWindowThreadProcessId};

    fn current_keyboard_layout() -> windows::Win32::UI::Input::KeyboardAndMouse::HKL {
        let foreground = unsafe { GetForegroundWindow() };
        if !foreground.0.is_null() {
            let mut process_id = 0u32;
            let thread_id = unsafe { GetWindowThreadProcessId(foreground, Some(&mut process_id)) };
            if thread_id != 0 {
                return unsafe { GetKeyboardLayout(thread_id) };
            }
        }

        unsafe { GetKeyboardLayout(0) }
    }

    let hkl = current_keyboard_layout();
    let mut result = HashMap::new();

    for probe in KEY_PROBES {
        let vk = unsafe { MapVirtualKeyExW(probe.scan_code, MAPVK_VSC_TO_VK_EX, Some(hkl)) };
        if vk == 0 {
            continue;
        }

        let base_state = [0u8; 256];

        let mut shift_state = [0u8; 256];
        shift_state[VK_SHIFT.0 as usize] = 0x80;
        shift_state[VK_RSHIFT.0 as usize] = 0x80;

        let mut altgr_state = [0u8; 256];
        altgr_state[VK_CONTROL.0 as usize] = 0x80;
        altgr_state[VK_MENU.0 as usize] = 0x80;
        altgr_state[VK_RCONTROL.0 as usize] = 0x80;
        altgr_state[VK_RMENU.0 as usize] = 0x80;

        let mut shift_altgr_state = altgr_state;
        shift_altgr_state[VK_SHIFT.0 as usize] = 0x80;
        shift_altgr_state[VK_RSHIFT.0 as usize] = 0x80;

        let base = extract_variant_for_state(hkl, vk, probe.scan_code, &base_state);
        let shift = extract_variant_for_state(hkl, vk, probe.scan_code, &shift_state);
        let alt_gr = extract_variant_for_state(hkl, vk, probe.scan_code, &altgr_state);
        let shift_alt_gr = extract_variant_for_state(hkl, vk, probe.scan_code, &shift_altgr_state);

        if base.is_none() && shift.is_none() && alt_gr.is_none() && shift_alt_gr.is_none() {
            continue;
        }

        result.insert(
            probe.code.to_string(),
            KbheOsKeyVariants {
                base,
                shift,
                alt_gr,
                shift_alt_gr,
            },
        );
    }

    Ok(result)
}

#[cfg(not(target_os = "windows"))]
fn get_os_key_variants_impl() -> Result<HashMap<String, KbheOsKeyVariants>, String> {
    Ok(HashMap::new())
}

#[tauri::command]
pub fn kbhe_get_os_key_variants() -> Result<HashMap<String, KbheOsKeyVariants>, String> {
    get_os_key_variants_impl()
}

// ---------------------------------------------------------------------------
// Firmware flash
// ---------------------------------------------------------------------------

const UPDATER_CMD_HELLO: u8 = 0x01;
const UPDATER_CMD_BEGIN: u8 = 0x02;
const UPDATER_CMD_DATA: u8 = 0x03;
const UPDATER_CMD_FINISH: u8 = 0x04;
const UPDATER_CMD_ABORT: u8 = 0x05;
const UPDATER_CMD_BOOT: u8 = 0x06;
const UPDATER_STATUS_OK: u8 = 0x00;
const APP_CMD_ENTER_BOOTLOADER: u8 = 0x02;
const DATA_CHUNK_SIZE: usize = 56;
const UPDATER_PROTOCOL_VERSION: u16 = 0x0001;
/// Device presence poll interval (matches Python DEVICE_POLL_DELAY_S = 0.02)
const DEVICE_POLL_MS: u64 = 20;

fn crc32_compute(data: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
        }
    }
    crc ^ 0xFFFF_FFFF
}

fn build_updater_write_buf(
    command: u8,
    sequence: u8,
    offset: u32,
    payload: &[u8],
) -> [u8; KBHE_PACKET_SIZE + 1] {
    let mut buf = [0u8; KBHE_PACKET_SIZE + 1];
    buf[0] = 0; // report ID
    buf[1] = command;
    buf[2] = sequence;
    buf[3] = 0;
    buf[4] = payload.len() as u8;
    buf[5..9].copy_from_slice(&offset.to_le_bytes());
    if !payload.is_empty() {
        buf[9..9 + payload.len()].copy_from_slice(payload);
    }
    buf
}

struct UpdaterResponse {
    status: u8,
    offset: u32,
    payload: Vec<u8>,
}

fn updater_transact(
    device: &HidDevice,
    command: u8,
    sequence: u8,
    offset: u32,
    payload: &[u8],
    retries: u32,
    timeout_ms: u64,
) -> Result<UpdaterResponse, String> {
    let write_buf = build_updater_write_buf(command, sequence, offset, payload);

    for attempt in 0..retries {
        device.write(&write_buf).map_err(|e| e.to_string())?;

        let deadline = Instant::now() + Duration::from_millis(timeout_ms);
        let mut rbuf = [0u8; KBHE_PACKET_SIZE];

        while Instant::now() < deadline {
            let remaining = deadline.saturating_duration_since(Instant::now());
            let t = i32::try_from(remaining.as_millis().max(1)).unwrap_or(i32::MAX);
            let n = device
                .read_timeout(&mut rbuf, t)
                .map_err(|e| e.to_string())?;
            if n > 0 && rbuf[0] == command {
                let status = rbuf[2];
                let length = rbuf[3] as usize;
                let resp_offset =
                    u32::from_le_bytes([rbuf[4], rbuf[5], rbuf[6], rbuf[7]]);
                let payload_end = (8 + length.min(DATA_CHUNK_SIZE)).min(n.max(8));
                let resp_payload = rbuf[8..payload_end].to_vec();
                return Ok(UpdaterResponse {
                    status,
                    offset: resp_offset,
                    payload: resp_payload,
                });
            }
        }

        if attempt + 1 < retries {
            // will retry
        }
    }

    Err(format!(
        "updater did not respond to command 0x{command:02X} after {retries} attempts"
    ))
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FlashProgress {
    pub phase: String,
    pub bytes_done: u32,
    pub total_bytes: u32,
    pub percent: u8,
}

fn emit_flash_progress(app: &AppHandle, phase: &str, bytes_done: u32, total_bytes: u32) {
    let percent = if total_bytes > 0 {
        ((bytes_done as u64 * 100) / total_bytes as u64) as u8
    } else {
        0
    };
    let _ = app.emit(
        "kbhe_flash_progress",
        FlashProgress {
            phase: phase.to_string(),
            bytes_done,
            total_bytes,
            percent,
        },
    );
}

fn find_updater_device() -> Result<Option<KbheHidDeviceInfo>, String> {
    find_first_device(KbheDeviceKind::Updater)
}

fn find_runtime_device() -> Result<Option<KbheHidDeviceInfo>, String> {
    find_first_device(KbheDeviceKind::Runtime)
}

fn wait_for_device_kind(
    find_fn: impl Fn() -> Result<Option<KbheHidDeviceInfo>, String>,
    timeout_ms: u64,
) -> Result<KbheHidDeviceInfo, String> {
    let deadline = Instant::now() + Duration::from_millis(timeout_ms);
    loop {
        if let Some(dev) = find_fn()? {
            return Ok(dev);
        }
        if Instant::now() >= deadline {
            return Err("timed out waiting for device".to_string());
        }
        std::thread::sleep(Duration::from_millis(DEVICE_POLL_MS));
    }
}

fn wait_for_device_absent(
    find_fn: impl Fn() -> Result<Option<KbheHidDeviceInfo>, String>,
    timeout_ms: u64,
) -> Result<(), String> {
    let deadline = Instant::now() + Duration::from_millis(timeout_ms);
    loop {
        if find_fn()?.is_none() {
            return Ok(());
        }
        if Instant::now() >= deadline {
            return Err("timed out waiting for device to disconnect".to_string());
        }
        std::thread::sleep(Duration::from_millis(DEVICE_POLL_MS));
    }
}

#[tauri::command]
pub async fn kbhe_flash_firmware(
    firmware_path: String,
    firmware_version: Option<u16>,
    app: AppHandle,
    state: State<'_, KbheTransportState>,
) -> Result<(), String> {
    {
        let mut active = lock_active(&state)?;
        *active = None;
    }

    tauri::async_runtime::spawn_blocking(move || {
        kbhe_flash_firmware_blocking(firmware_path, firmware_version, app)
    })
    .await
    .map_err(|error| format!("firmware flash worker failed: {error}"))?
}

fn kbhe_flash_firmware_blocking(
    firmware_path: String,
    firmware_version: Option<u16>,
    app: AppHandle,
) -> Result<(), String> {
    let firmware =
        std::fs::read(&firmware_path).map_err(|e| format!("failed to read firmware: {e}"))?;
    if firmware.is_empty() {
        return Err("firmware file is empty".to_string());
    }

    let aligned_len = (firmware.len() + 3) & !3;
    let mut padded = firmware.clone();
    padded.resize(aligned_len, 0xFF);

    let image_crc32 = crc32_compute(&firmware);
    let fw_version = firmware_version.unwrap_or(0);
    let total = firmware.len() as u32;

    emit_flash_progress(&app, "connecting", 0, total);

    // Request bootloader from runtime device if not already in updater mode
    if find_updater_device()?.is_none() {
        // Open a fresh connection to the runtime device and send ENTER_BOOTLOADER.
        // This works whether or not the TypeScript side already disconnected — we
        // always enumerate from scratch so we're not relying on state.active.
        if let Some(runtime_info) = find_runtime_device()? {
            if let Ok(runtime_device) = open_device_by_path(&runtime_info.path) {
                let mut report = [0u8; KBHE_PACKET_SIZE + 1];
                report[1] = APP_CMD_ENTER_BOOTLOADER;
                let _ = runtime_device.write(&report);
                let mut tmp = [0u8; KBHE_PACKET_SIZE];
                let _ = runtime_device.read_timeout(&mut tmp, 500);
                // runtime_device dropped here — HID handle released before
                // the device re-enumerates as updater
            }
        }

        // Wait for runtime device to disconnect
        let _ = wait_for_device_absent(find_runtime_device, 5_000);
    }

    // Wait for updater device
    let updater_info = wait_for_device_kind(find_updater_device, 8_000)
        .map_err(|_| "updater device not found after requesting bootloader mode".to_string())?;

    let device = open_device_by_path(&updater_info.path)?;

    let result = flash_with_device(&device, &firmware, &padded, image_crc32, fw_version, &app, total);

    if result.is_err() {
        // Best-effort ABORT
        let abort_buf = build_updater_write_buf(UPDATER_CMD_ABORT, 0xFF, 0, &[]);
        let _ = device.write(&abort_buf);
    }

    result?;

    // Wait for updater to disconnect then app to come back
    let _ = wait_for_device_absent(find_updater_device, 10_000);
    wait_for_device_kind(find_runtime_device, 15_000)
        .map_err(|_| "application did not come back after flashing".to_string())?;

    emit_flash_progress(&app, "done", total, total);
    Ok(())
}

fn flash_with_device(
    device: &HidDevice,
    firmware: &[u8],
    padded: &[u8],
    image_crc32: u32,
    fw_version: u16,
    app: &AppHandle,
    total: u32,
) -> Result<(), String> {
    let mut seq = 1u8;

    // HELLO
    emit_flash_progress(app, "hello", 0, total);
    let hello = updater_transact(device, UPDATER_CMD_HELLO, seq, 0, &[], 5, 3_000)?;
    if hello.status != UPDATER_STATUS_OK {
        return Err(format!("HELLO failed: status 0x{:02X}", hello.status));
    }
    if hello.payload.len() >= 20 {
        let proto = u16::from_le_bytes([hello.payload[0], hello.payload[1]]);
        if proto != UPDATER_PROTOCOL_VERSION {
            return Err(format!(
                "unsupported updater protocol 0x{proto:04X}, expected 0x{UPDATER_PROTOCOL_VERSION:04X}"
            ));
        }
        let max_size = u32::from_le_bytes([
            hello.payload[8],
            hello.payload[9],
            hello.payload[10],
            hello.payload[11],
        ]);
        if max_size > 0 && firmware.len() as u32 > max_size {
            return Err(format!(
                "firmware is too large ({} bytes), updater max is {max_size} bytes",
                firmware.len()
            ));
        }
    }

    // BEGIN: <IIHH> = firmware_len, crc32, fw_version, 0
    seq = seq.wrapping_add(1);
    emit_flash_progress(app, "begin", 0, total);
    let mut begin_payload = [0u8; 12];
    begin_payload[0..4].copy_from_slice(&(firmware.len() as u32).to_le_bytes());
    begin_payload[4..8].copy_from_slice(&image_crc32.to_le_bytes());
    begin_payload[8..10].copy_from_slice(&fw_version.to_le_bytes());
    let begin = updater_transact(device, UPDATER_CMD_BEGIN, seq, 0, &begin_payload, 5, 3_000)?;
    if begin.status != UPDATER_STATUS_OK {
        return Err(format!("BEGIN failed: status 0x{:02X}", begin.status));
    }

    // DATA chunks
    let mut offset = 0usize;
    let mut last_percent = u8::MAX;
    while offset < padded.len() {
        let end = (offset + DATA_CHUNK_SIZE).min(padded.len());
        let chunk = &padded[offset..end];
        seq = seq.wrapping_add(1);
        let resp = updater_transact(device, UPDATER_CMD_DATA, seq, offset as u32, chunk, 5, 3_000)?;
        if resp.status != UPDATER_STATUS_OK {
            return Err(format!(
                "DATA failed at offset 0x{offset:08X}: status 0x{:02X}",
                resp.status
            ));
        }
        let next_offset = resp.offset as usize;
        if next_offset != offset + chunk.len() {
            return Err(format!(
                "offset mismatch: updater acked 0x{next_offset:08X}, expected 0x{:08X}",
                offset + chunk.len()
            ));
        }
        offset = next_offset;

        let progress = (offset.min(firmware.len()) as u32).min(total);
        let percent = if total > 0 {
            ((progress as u64 * 100) / total as u64) as u8
        } else {
            100
        };
        if percent != last_percent {
            emit_flash_progress(app, "flashing", progress, total);
            last_percent = percent;
        }
    }

    // FINISH
    seq = seq.wrapping_add(1);
    emit_flash_progress(app, "finish", total, total);
    let finish = updater_transact(device, UPDATER_CMD_FINISH, seq, 0, &[], 5, 5_000)?;
    if finish.status != UPDATER_STATUS_OK {
        return Err(format!("FINISH failed: status 0x{:02X}", finish.status));
    }

    // BOOT (device may not respond — best effort)
    seq = seq.wrapping_add(1);
    emit_flash_progress(app, "boot", total, total);
    let _ = updater_transact(device, UPDATER_CMD_BOOT, seq, 0, &[], 3, 2_000);

    Ok(())
}
