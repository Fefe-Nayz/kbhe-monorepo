use hidapi::{DeviceInfo, HidApi, HidDevice};
use serde::Serialize;
use std::ffi::CString;
use std::sync::{Mutex, MutexGuard};
use std::time::{Duration, Instant};
use tauri::State;

const KBHE_VID: u16 = 0x9172;
const KBHE_APP_PID: u16 = 0x0002;
const KBHE_UPDATER_PID: u16 = 0x0003;
const KBHE_RAW_HID_USAGE_PAGE: u16 = 0xFF00;
const KBHE_APP_RAW_HID_INTERFACE: i32 = 1;
const KBHE_PACKET_SIZE: usize = 64;

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

#[tauri::command]
pub fn kbhe_list_devices() -> Result<Vec<KbheHidDeviceInfo>, String> {
    enumerate_kbhe_devices()
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
