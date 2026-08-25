use crate::releases::{
    firmware_asset_name_for_version, is_firmware_carrier_name, REFRESH_FIRMWARE_ASSET_NAME,
};
use crate::signing::firmware_manifest;
use crate::updater_compat::{
    bootloader_refresh_required, inspect_firmware_artifact, negotiate_flash_protocol,
    parse_bootloader_info, parse_updater_hello, updater_cleanup_is_safe,
    verify_refreshed_bootloader, BootloaderInfo, FirmwareArtifact, FlashProtocol, UpdaterHello,
    UPDATER_FLAG_APP_VALID, UPDATER_PROTOCOL_V2, UPDATER_PROTOCOL_V3,
    UPDATER_V2_APP_MAX_IMAGE_SIZE,
};
use hidapi::{DeviceInfo, HidApi, HidDevice};
use semver::Version as SemverVersion;
use serde::Serialize;
use std::collections::HashMap;
use std::ffi::CString;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Mutex, MutexGuard,
};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, State};

const KBHE_VID: u16 = 0x9172;
const KBHE_APP_PID: u16 = 0x0002;
const KBHE_UPDATER_PID: u16 = 0x0003;
const KBHE_LIBHMK_PID: u16 = 0x0004;
const KBHE_RAW_HID_USAGE_PAGE: u16 = 0xFF00;
const KBHE_APP_RAW_HID_INTERFACE: i32 = 1;
const KBHE_LIBHMK_USAGE_PAGE: u16 = 0xFFAB;
const KBHE_LIBHMK_USAGE: u16 = 0x00AB;
const KBHE_PACKET_SIZE: usize = 64;
const KBHE_KEY_COUNT: usize = 82;
const KBHE_KEY_STATES_PER_CHUNK: usize = 15;
const CMD_GET_KEY_STATES: u8 = 0xE1;
const STATUS_OK: u8 = 0x00;
const RGB_BRIDGE_PROTOCOL_MAJOR: u8 = 1;
const RGB_CMD_GET_ENABLED: u8 = 0x60;
const RGB_CMD_SET_ENABLED: u8 = 0x61;
const RGB_CMD_GET_BRIGHTNESS: u8 = 0x62;
const RGB_CMD_SET_BRIGHTNESS: u8 = 0x63;
const RGB_CMD_SET_PIXEL: u8 = 0x65;
const RGB_CMD_SET_FRAME_CHUNK: u8 = 0x6A;
const RGB_CMD_CLEAR: u8 = 0x6B;
const RGB_CMD_FILL: u8 = 0x6C;
const RGB_CMD_GET_EFFECT: u8 = 0x6E;
const RGB_CMD_SET_EFFECT: u8 = 0x6F;
const RGB_CMD_RESTORE_EFFECT: u8 = 0x76;
const RGB_CMD_GET_CAPABILITIES: u8 = 0x7F;
const RGB_CAP_ENABLED: u16 = 1 << 0;
const RGB_CAP_BRIGHTNESS: u16 = 1 << 1;
const RGB_CAP_PIXEL: u16 = 1 << 2;
const RGB_CAP_FRAME_CHUNKS: u16 = 1 << 3;
const RGB_CAP_FILL: u16 = 1 << 4;
const RGB_CAP_LIVE_MODE: u16 = 1 << 5;
const RGB_CAP_RESTORE_MODE: u16 = 1 << 6;
static FIRMWARE_FLASH_ACTIVE: AtomicBool = AtomicBool::new(false);
static RGB_BRIDGE_OPERATION: Mutex<()> = Mutex::new(());

struct ExclusiveOperationGuard(&'static AtomicBool);

impl ExclusiveOperationGuard {
    fn acquire(flag: &'static AtomicBool, label: &str) -> Result<Self, String> {
        flag.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .map(|_| Self(flag))
            .map_err(|_| format!("{label} is already in progress"))
    }
}

impl Drop for ExclusiveOperationGuard {
    fn drop(&mut self) {
        self.0.store(false, Ordering::Release);
    }
}

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

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct KbheUpdaterInfo {
    protocol_version: u16,
    flags: u16,
    app_base: u32,
    app_max_size: u32,
    write_align: u32,
    installed_version: [u8; 3],
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct KbheRgbBridgeDeviceInfo {
    path: String,
    vid: u16,
    pid: u16,
    interface_number: Option<i32>,
    usage_page: Option<u16>,
    usage: Option<u16>,
    manufacturer: Option<String>,
    product: Option<String>,
    serial_number: Option<String>,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct KbheRgbBridgeCapabilities {
    protocol_major: u8,
    protocol_minor: u8,
    led_count: u8,
    bytes_per_pixel: u8,
    chunk_bytes: u8,
    live_effect_id: u8,
    capabilities: u16,
    color_order: u8,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct KbheRgbBridgeState {
    capabilities: KbheRgbBridgeCapabilities,
    enabled: Option<bool>,
    brightness: Option<u8>,
    effect: u8,
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

fn firmware_flash_blocks_transport(flash_active: bool) -> bool {
    flash_active
}

fn ensure_transport_not_flashing() -> Result<(), String> {
    if firmware_flash_blocks_transport(FIRMWARE_FLASH_ACTIVE.load(Ordering::Acquire)) {
        return Err("firmware flashing is in progress".to_string());
    }
    Ok(())
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

fn matches_libhmk_rgb_identity(vid: u16, pid: u16, usage_page: u16, usage: u16) -> bool {
    vid == KBHE_VID
        && pid == KBHE_LIBHMK_PID
        && usage_page == KBHE_LIBHMK_USAGE_PAGE
        && usage == KBHE_LIBHMK_USAGE
}

fn matches_libhmk_rgb_bridge(info: &DeviceInfo) -> bool {
    matches_libhmk_rgb_identity(
        info.vendor_id(),
        info.product_id(),
        info.usage_page(),
        info.usage(),
    )
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
    if info.product_id() == KBHE_LIBHMK_PID {
        return false;
    }

    let product = info
        .product_string()
        .map(|value| value.to_ascii_lowercase())
        .unwrap_or_default();
    let manufacturer = info
        .manufacturer_string()
        .map(|value| value.to_ascii_lowercase())
        .unwrap_or_default();

    let looks_like_update_mode =
        product.contains("bootloader") || product.contains("updater") || product.contains("dfu");

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

fn rgb_bridge_device_info_from(info: &DeviceInfo) -> Option<KbheRgbBridgeDeviceInfo> {
    if !matches_libhmk_rgb_bridge(info) {
        return None;
    }

    Some(KbheRgbBridgeDeviceInfo {
        path: path_to_string(info),
        vid: info.vendor_id(),
        pid: info.product_id(),
        interface_number: optional_interface_number(info),
        usage_page: optional_usage_page(info),
        usage: optional_usage(info),
        manufacturer: info.manufacturer_string().map(|value| value.to_string()),
        product: info.product_string().map(|value| value.to_string()),
        serial_number: info.serial_number().map(|value| value.to_string()),
    })
}

fn enumerate_rgb_bridge_devices() -> Result<Vec<KbheRgbBridgeDeviceInfo>, String> {
    let api = HidApi::new().map_err(|error| error.to_string())?;
    Ok(api
        .device_list()
        .filter_map(rgb_bridge_device_info_from)
        .collect())
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

fn device_kind_label(kind: KbheDeviceKind) -> &'static str {
    match kind {
        KbheDeviceKind::Runtime => "runtime",
        KbheDeviceKind::Updater => "updater",
    }
}

fn select_unique_flash_device(
    devices: &[KbheHidDeviceInfo],
    kind: KbheDeviceKind,
    expected_serial: &str,
) -> Result<Option<KbheHidDeviceInfo>, String> {
    let matches = devices
        .iter()
        .filter(|device| {
            device.kind == kind
                && device.serial_number.as_deref().map(str::trim) == Some(expected_serial)
        })
        .cloned()
        .collect::<Vec<_>>();

    match matches.len() {
        0 => Ok(None),
        1 => Ok(matches.into_iter().next()),
        count => Err(format!(
            "refusing ambiguous firmware target: found {count} {} devices with serial {expected_serial}",
            device_kind_label(kind)
        )),
    }
}

fn resolve_flash_target_snapshot(
    devices: &[KbheHidDeviceInfo],
    expected_serial: &str,
) -> Result<(Option<KbheHidDeviceInfo>, Option<KbheHidDeviceInfo>), String> {
    let runtime = select_unique_flash_device(devices, KbheDeviceKind::Runtime, expected_serial)?;
    let updater = select_unique_flash_device(devices, KbheDeviceKind::Updater, expected_serial)?;
    if runtime.is_some() && updater.is_some() {
        return Err(format!(
            "refusing ambiguous firmware target: serial {expected_serial} is present in both runtime and updater mode"
        ));
    }
    Ok((runtime, updater))
}

fn find_flash_device(
    kind: KbheDeviceKind,
    expected_serial: &str,
) -> Result<Option<KbheHidDeviceInfo>, String> {
    select_unique_flash_device(&enumerate_kbhe_devices()?, kind, expected_serial)
}

fn verify_open_device_serial(device: &HidDevice, expected_serial: &str) -> Result<(), String> {
    let actual = device
        .get_serial_number_string()
        .map_err(|error| format!("failed to read opened HID serial number: {error}"))?
        .ok_or_else(|| "opened HID device exposes no serial number".to_string())?;
    if actual.trim() != expected_serial {
        return Err(format!(
            "opened HID device serial changed: expected {expected_serial}, got {}",
            actual.trim()
        ));
    }
    Ok(())
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
    if data.len() > KBHE_PACKET_SIZE - 1 {
        return Err(format!(
            "RAW HID command payload has {} bytes; maximum is {}",
            data.len(),
            KBHE_PACKET_SIZE - 1
        ));
    }
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

    let written = connection
        .device
        .write(&report)
        .map_err(|error| error.to_string())?;
    if written != report.len() {
        return Err(format!(
            "short RAW HID write: wrote {written} of {} bytes",
            report.len()
        ));
    }

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
        if response.len() >= 2
            && response[0] == command
            && response_matches_request(command, data, &response)
        {
            return Ok(Some(response));
        }
    }

    Ok(None)
}

fn response_matches_request(command: u8, data: &[u8], response: &[u8]) -> bool {
    let byte = |slice: &[u8], index: usize| slice.get(index).copied();
    match command {
        // GET_KEY_SETTINGS: key/profile/layer are echoed in the response.
        0x40 => {
            byte(data, 1) == byte(response, 2)
                && byte(data, 2) == byte(response, 3)
                && byte(data, 3) == byte(response, 4)
        }
        // Indexed/chunked runtime reads. The requested index is data[1] and
        // the response echoes it at byte 2.
        0x46 | 0x49 | 0x4B | 0x4F | 0x5C | 0x64 | 0x66 | 0x68 | 0xE1 | 0xE6 | 0xE7 | 0xE8 => {
            byte(data, 1) == byte(response, 2)
        }
        // GET_LAYER_KEYCODE: layer and key are both echoed.
        0x56 => byte(data, 1) == byte(response, 2) && byte(data, 2) == byte(response, 3),
        // Action program/overlay reads use an explicit request length byte.
        0x91 | 0x97 => byte(data, 1) == byte(response, 2) && byte(data, 2) == byte(response, 3),
        0x92 => {
            byte(data, 1) == byte(response, 2)
                && byte(data, 2) == byte(response, 3)
                && byte(data, 3) == byte(response, 4)
        }
        // ProfileDocument metadata echoes the selected profile.
        0x9C => byte(data, 1) == byte(response, 2),
        // Persistent and session-only mode writes echo state index and value.
        0x9A | 0x9D => byte(data, 1) == byte(response, 2) && byte(data, 2) == byte(response, 3),
        _ => true,
    }
}

fn rgb_bridge_report(command: u8, payload: &[u8]) -> Result<[u8; KBHE_PACKET_SIZE + 1], String> {
    if payload.len() > KBHE_PACKET_SIZE - 2 {
        return Err(format!(
            "RGB bridge payload has {} bytes; maximum is {}",
            payload.len(),
            KBHE_PACKET_SIZE - 2
        ));
    }

    let mut report = [0u8; KBHE_PACKET_SIZE + 1];
    report[1] = command;
    report[3..3 + payload.len()].copy_from_slice(payload);
    Ok(report)
}

fn normalize_rgb_bridge_response(raw: &[u8]) -> Result<[u8; KBHE_PACKET_SIZE], String> {
    let payload = if raw.len() == KBHE_PACKET_SIZE + 1 && raw[0] == 0 {
        &raw[1..]
    } else {
        raw
    };
    payload.try_into().map_err(|_| {
        format!(
            "RGB bridge returned {} bytes; expected {}",
            payload.len(),
            KBHE_PACKET_SIZE
        )
    })
}

fn rgb_bridge_response_matches_request(command: u8, payload: &[u8], response: &[u8]) -> bool {
    match command {
        RGB_CMD_SET_ENABLED | RGB_CMD_SET_BRIGHTNESS | RGB_CMD_SET_EFFECT => {
            payload.first() == response.get(2)
        }
        RGB_CMD_SET_PIXEL => payload.get(..4) == response.get(2..6),
        RGB_CMD_SET_FRAME_CHUNK => payload.get(..2) == response.get(2..4),
        RGB_CMD_FILL => payload.get(..3) == response.get(2..5),
        _ => true,
    }
}

fn rgb_bridge_exchange(
    device: &HidDevice,
    command: u8,
    payload: &[u8],
) -> Result<[u8; KBHE_PACKET_SIZE], String> {
    let mut stale = [0u8; KBHE_PACKET_SIZE + 1];
    loop {
        let read = device
            .read_timeout(&mut stale, 0)
            .map_err(|error| format!("failed to flush RGB bridge input: {error}"))?;
        if read == 0 {
            break;
        }
    }

    let report = rgb_bridge_report(command, payload)?;
    let written = device
        .write(&report)
        .map_err(|error| format!("failed to write RGB bridge report: {error}"))?;
    // hidapi backends disagree on whether the leading report ID contributes to
    // the returned byte count. Both forms still represent one complete report.
    if written != KBHE_PACKET_SIZE && written != KBHE_PACKET_SIZE + 1 {
        return Err(format!(
            "short RGB bridge write: wrote {written} bytes, expected {} or {}",
            KBHE_PACKET_SIZE,
            KBHE_PACKET_SIZE + 1
        ));
    }

    let deadline = Instant::now() + Duration::from_millis(1_000);
    let mut raw = [0u8; KBHE_PACKET_SIZE + 1];
    while Instant::now() < deadline {
        let remaining = deadline.saturating_duration_since(Instant::now());
        let timeout_ms = i32::try_from(remaining.as_millis().max(1)).unwrap_or(i32::MAX);
        let read = device
            .read_timeout(&mut raw, timeout_ms)
            .map_err(|error| format!("failed to read RGB bridge response: {error}"))?;
        if read == 0 {
            continue;
        }

        let response = normalize_rgb_bridge_response(&raw[..read])?;
        if response[0] != command {
            // A prior timed-out GET may still arrive after the pre-write flush.
            // Ignore only a different command; the matching response below is
            // still required before the bounded deadline.
            continue;
        }
        if response[1] != STATUS_OK {
            return Err(format!(
                "RGB bridge command 0x{command:02X} was rejected with status {}",
                response[1]
            ));
        }
        if !rgb_bridge_response_matches_request(command, payload, &response) {
            continue;
        }
        return Ok(response);
    }

    Err(format!(
        "timed out waiting for RGB bridge command 0x{command:02X}"
    ))
}

fn parse_rgb_bridge_capabilities(
    response: &[u8; KBHE_PACKET_SIZE],
) -> Result<KbheRgbBridgeCapabilities, String> {
    if response[0] != RGB_CMD_GET_CAPABILITIES || response[1] != STATUS_OK {
        return Err("invalid RGB bridge capability response header".to_string());
    }

    let capabilities = KbheRgbBridgeCapabilities {
        protocol_major: response[2],
        protocol_minor: response[3],
        led_count: response[4],
        bytes_per_pixel: response[5],
        chunk_bytes: response[6],
        live_effect_id: response[7],
        capabilities: u16::from_le_bytes([response[8], response[9]]),
        color_order: response[10],
    };

    if capabilities.protocol_major != RGB_BRIDGE_PROTOCOL_MAJOR {
        return Err(format!(
            "unsupported RGB bridge protocol {}.{}",
            capabilities.protocol_major, capabilities.protocol_minor
        ));
    }
    if capabilities.led_count == 0
        || capabilities.bytes_per_pixel != 3
        || capabilities.chunk_bytes == 0
        || usize::from(capabilities.chunk_bytes) > KBHE_PACKET_SIZE - 4
        || capabilities.live_effect_id != 7
        || capabilities.color_order != 0
    {
        return Err("RGB bridge returned invalid geometry or color order".to_string());
    }
    let frame_bytes =
        usize::from(capabilities.led_count) * usize::from(capabilities.bytes_per_pixel);
    let chunk_count = frame_bytes.div_ceil(usize::from(capabilities.chunk_bytes));
    if chunk_count > usize::from(u8::MAX) + 1 {
        return Err("RGB bridge frame requires too many chunks".to_string());
    }

    Ok(capabilities)
}

fn require_rgb_capability(
    capabilities: &KbheRgbBridgeCapabilities,
    capability: u16,
    label: &str,
) -> Result<(), String> {
    if capabilities.capabilities & capability == 0 {
        return Err(format!("RGB bridge does not advertise {label}"));
    }
    Ok(())
}

fn is_supported_libhmk_rgb_effect(effect: u8) -> bool {
    matches!(effect, 0 | 1 | 2 | 3 | 7)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct RgbLiveWritePlan {
    enter_live: bool,
    restore_effect_on_failure: bool,
}

fn rgb_live_write_plan(current_effect: u8, live_effect: u8) -> RgbLiveWritePlan {
    let transition = current_effect != live_effect;
    RgbLiveWritePlan {
        enter_live: transition,
        restore_effect_on_failure: transition,
    }
}

fn rgb_bridge_get_effect(device: &HidDevice) -> Result<u8, String> {
    let effect = rgb_bridge_exchange(device, RGB_CMD_GET_EFFECT, &[])?[2];
    if !is_supported_libhmk_rgb_effect(effect) {
        return Err(format!("RGB bridge returned unsupported effect {effect}"));
    }
    Ok(effect)
}

fn rgb_bridge_set_effect_on_device(device: &HidDevice, effect: u8) -> Result<(), String> {
    if !is_supported_libhmk_rgb_effect(effect) {
        return Err(format!("unsupported libhmk RGB effect {effect}"));
    }
    let response = rgb_bridge_exchange(device, RGB_CMD_SET_EFFECT, &[effect])?;
    if response[2] != effect {
        return Err("RGB bridge did not echo the requested effect".to_string());
    }
    Ok(())
}

fn rgb_bridge_rollback_error(operation_error: String, rollback: Result<(), String>) -> String {
    match rollback {
        Ok(()) => operation_error,
        Err(rollback_error) => {
            format!("{operation_error}; RGB effect rollback also failed: {rollback_error}")
        }
    }
}

fn rgb_bridge_restore_original_effect(
    device: &HidDevice,
    original_effect: u8,
    persistent_effect: u8,
) -> Result<(), String> {
    rgb_bridge_set_effect_on_device(device, persistent_effect)?;
    if original_effect == 7 {
        rgb_bridge_set_effect_on_device(device, 7)?;
    }
    Ok(())
}

fn rgb_bridge_prepare_static_write(
    device: &HidDevice,
    capabilities: &KbheRgbBridgeCapabilities,
) -> Result<(u8, u8), String> {
    let original_effect = rgb_bridge_get_effect(device)?;
    let persistent_effect = if original_effect == capabilities.live_effect_id {
        require_rgb_capability(
            capabilities,
            RGB_CAP_RESTORE_MODE,
            "effect restore for static-write rollback",
        )?;
        let response = rgb_bridge_exchange(device, RGB_CMD_RESTORE_EFFECT, &[])?;
        let restored = response[2];
        if !is_supported_libhmk_rgb_effect(restored) || restored == 7 {
            return Err(format!(
                "RGB bridge restored an invalid persistent effect {restored}"
            ));
        }
        restored
    } else {
        original_effect
    };

    if let Err(error) = rgb_bridge_set_effect_on_device(device, 0) {
        let rollback = if original_effect == 7 {
            rgb_bridge_set_effect_on_device(device, 7)
        } else {
            Ok(())
        };
        return Err(rgb_bridge_rollback_error(error, rollback));
    }
    Ok((original_effect, persistent_effect))
}

fn select_unique_rgb_bridge_device(
    devices: &[KbheRgbBridgeDeviceInfo],
    path: &str,
    expected_serial_number: &str,
) -> Result<KbheRgbBridgeDeviceInfo, String> {
    let expected_serial_number = expected_serial_number.trim();
    if expected_serial_number.is_empty() {
        return Err("libhmk RGB control requires a non-empty USB serial number".to_string());
    }

    let matches = devices
        .iter()
        .filter(|device| {
            device.serial_number.as_deref().map(str::trim) == Some(expected_serial_number)
        })
        .collect::<Vec<_>>();
    if matches.len() > 1 {
        return Err(format!(
            "refusing ambiguous libhmk RGB target: found {} devices with serial {expected_serial_number}",
            matches.len()
        ));
    }
    let matched = matches.first().copied().ok_or_else(|| {
        format!("libhmk RGB device serial {expected_serial_number} is no longer present")
    })?;
    if matched.path != path {
        return Err(format!(
            "libhmk RGB path no longer belongs to keyboard serial {expected_serial_number}"
        ));
    }
    Ok(matched.clone())
}

fn open_verified_rgb_bridge(
    path: &str,
    expected_serial_number: &str,
) -> Result<(HidDevice, KbheRgbBridgeCapabilities), String> {
    let devices = enumerate_rgb_bridge_devices()?;
    let matched = select_unique_rgb_bridge_device(&devices, path, expected_serial_number)?;
    let device = open_device_by_path(&matched.path)?;
    verify_open_device_serial(&device, expected_serial_number.trim())?;
    let response = rgb_bridge_exchange(&device, RGB_CMD_GET_CAPABILITIES, &[])?;
    let capabilities = parse_rgb_bridge_capabilities(&response)?;
    Ok((device, capabilities))
}

fn lock_rgb_bridge_operation() -> Result<MutexGuard<'static, ()>, String> {
    RGB_BRIDGE_OPERATION
        .lock()
        .map_err(|_| "RGB bridge operation lock is poisoned".to_string())
}

#[tauri::command]
pub fn kbhe_list_devices() -> Result<Vec<KbheHidDeviceInfo>, String> {
    enumerate_kbhe_devices()
}

#[tauri::command]
pub fn kbhe_get_updater_info(
    state: State<'_, KbheTransportState>,
) -> Result<KbheUpdaterInfo, String> {
    ensure_transport_not_flashing()?;
    let active = lock_active(&state)?;
    let connection = active
        .as_ref()
        .ok_or_else(|| "no KBHE updater session is connected".to_string())?;
    if connection.kind != KbheDeviceKind::Updater {
        return Err("the active KBHE session is not an updater".to_string());
    }

    let response = updater_transact(&connection.device, UPDATER_CMD_HELLO, 1, 0, &[], 2, 750)?;
    if response.status != UPDATER_STATUS_OK {
        return Err(updater_status_error("HELLO", response.status));
    }
    let hello = parse_updater_hello(&response.payload)?;
    Ok(KbheUpdaterInfo {
        protocol_version: hello.protocol_version,
        flags: hello.flags,
        app_base: hello.app_base,
        app_max_size: hello.app_max_size,
        write_align: hello.write_align,
        installed_version: hello.installed_version,
    })
}

#[tauri::command]
pub fn kbhe_list_rgb_bridge_devices() -> Result<Vec<KbheRgbBridgeDeviceInfo>, String> {
    enumerate_rgb_bridge_devices()
}

#[tauri::command]
pub fn kbhe_rgb_bridge_get_state(
    path: String,
    expected_serial_number: String,
) -> Result<KbheRgbBridgeState, String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;

    let enabled = if capabilities.capabilities & RGB_CAP_ENABLED != 0 {
        let response = rgb_bridge_exchange(&device, RGB_CMD_GET_ENABLED, &[])?;
        if response[2] > 1 {
            return Err("RGB bridge returned an invalid enabled state".to_string());
        }
        Some(response[2] != 0)
    } else {
        None
    };

    let brightness = if capabilities.capabilities & RGB_CAP_BRIGHTNESS != 0 {
        Some(rgb_bridge_exchange(&device, RGB_CMD_GET_BRIGHTNESS, &[])?[2])
    } else {
        None
    };
    let effect = rgb_bridge_get_effect(&device)?;

    Ok(KbheRgbBridgeState {
        capabilities,
        enabled,
        brightness,
        effect,
    })
}

#[tauri::command]
pub fn kbhe_rgb_bridge_set_enabled(
    path: String,
    expected_serial_number: String,
    enabled: bool,
) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_ENABLED, "enabled control")?;
    let expected = u8::from(enabled);
    let response = rgb_bridge_exchange(&device, RGB_CMD_SET_ENABLED, &[expected])?;
    if response[2] != expected {
        return Err("RGB bridge did not echo the requested enabled state".to_string());
    }
    Ok(())
}

#[tauri::command]
pub fn kbhe_rgb_bridge_set_brightness(
    path: String,
    expected_serial_number: String,
    brightness: u8,
) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_BRIGHTNESS, "brightness control")?;
    let response = rgb_bridge_exchange(&device, RGB_CMD_SET_BRIGHTNESS, &[brightness])?;
    if response[2] != brightness {
        return Err("RGB bridge did not echo the requested brightness".to_string());
    }
    Ok(())
}

#[tauri::command]
pub fn kbhe_rgb_bridge_fill(
    path: String,
    expected_serial_number: String,
    r: u8,
    g: u8,
    b: u8,
) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_FILL, "fill control")?;
    let (original_effect, persistent_effect) =
        rgb_bridge_prepare_static_write(&device, &capabilities)?;
    let color = [r, g, b];
    let fill_result = rgb_bridge_exchange(&device, RGB_CMD_FILL, &color).and_then(|response| {
        if response[2..5] != color {
            return Err("RGB bridge did not echo the requested fill color".to_string());
        }
        Ok(())
    });
    fill_result.map_err(|error| {
        rgb_bridge_rollback_error(
            error,
            rgb_bridge_restore_original_effect(&device, original_effect, persistent_effect),
        )
    })
}

#[tauri::command]
pub fn kbhe_rgb_bridge_clear(path: String, expected_serial_number: String) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_FILL, "fill control")?;
    let (original_effect, persistent_effect) =
        rgb_bridge_prepare_static_write(&device, &capabilities)?;
    rgb_bridge_exchange(&device, RGB_CMD_CLEAR, &[])
        .map(|_| ())
        .map_err(|error| {
            rgb_bridge_rollback_error(
                error,
                rgb_bridge_restore_original_effect(&device, original_effect, persistent_effect),
            )
        })
}

#[tauri::command]
pub fn kbhe_rgb_bridge_restore_effect(
    path: String,
    expected_serial_number: String,
) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_RESTORE_MODE, "effect restore")?;
    rgb_bridge_exchange(&device, RGB_CMD_RESTORE_EFFECT, &[])?;
    Ok(())
}

#[tauri::command]
pub fn kbhe_rgb_bridge_set_effect(
    path: String,
    expected_serial_number: String,
    effect: u8,
) -> Result<(), String> {
    if !is_supported_libhmk_rgb_effect(effect) {
        return Err(format!("unsupported libhmk RGB effect {effect}"));
    }

    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    if effect == capabilities.live_effect_id {
        require_rgb_capability(&capabilities, RGB_CAP_LIVE_MODE, "live mode")?;
    } else if effect == 7 {
        return Err(format!(
            "device advertises live effect {}, not 7",
            capabilities.live_effect_id
        ));
    }
    rgb_bridge_set_effect_on_device(&device, effect)
}

#[tauri::command]
pub fn kbhe_rgb_bridge_set_pixel(
    path: String,
    expected_serial_number: String,
    index: u8,
    r: u8,
    g: u8,
    b: u8,
) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_PIXEL, "individual-pixel control")?;
    require_rgb_capability(&capabilities, RGB_CAP_LIVE_MODE, "live mode")?;
    if index >= capabilities.led_count {
        return Err(format!(
            "RGB pixel index {index} is outside the advertised {} LEDs",
            capabilities.led_count
        ));
    }
    let previous_effect = rgb_bridge_get_effect(&device)?;
    let plan = rgb_live_write_plan(previous_effect, capabilities.live_effect_id);
    if plan.enter_live {
        require_rgb_capability(
            &capabilities,
            RGB_CAP_RESTORE_MODE,
            "effect restore for failed pixel rollback",
        )?;
        rgb_bridge_set_effect_on_device(&device, capabilities.live_effect_id)?;
    }
    let pixel = [index, r, g, b];
    let pixel_result =
        rgb_bridge_exchange(&device, RGB_CMD_SET_PIXEL, &pixel).and_then(|response| {
            if response[2..6] != pixel {
                return Err("RGB bridge did not echo the requested pixel".to_string());
            }
            Ok(())
        });
    pixel_result.map_err(|error| {
        let rollback = if plan.restore_effect_on_failure {
            rgb_bridge_exchange(&device, RGB_CMD_RESTORE_EFFECT, &[]).map(|_| ())
        } else {
            Ok(())
        };
        rgb_bridge_rollback_error(error, rollback)
    })
}

#[tauri::command]
pub fn kbhe_rgb_bridge_write_frame(
    path: String,
    expected_serial_number: String,
    frame: Vec<u8>,
) -> Result<(), String> {
    let _operation = lock_rgb_bridge_operation()?;
    let (device, capabilities) = open_verified_rgb_bridge(&path, &expected_serial_number)?;
    require_rgb_capability(&capabilities, RGB_CAP_FRAME_CHUNKS, "frame upload")?;
    require_rgb_capability(&capabilities, RGB_CAP_LIVE_MODE, "live mode")?;

    let expected_len =
        usize::from(capabilities.led_count) * usize::from(capabilities.bytes_per_pixel);
    if frame.len() != expected_len {
        return Err(format!(
            "RGB frame has {} bytes; expected {expected_len}",
            frame.len()
        ));
    }

    let previous_effect = rgb_bridge_get_effect(&device)?;
    let plan = rgb_live_write_plan(previous_effect, capabilities.live_effect_id);
    if plan.enter_live {
        require_rgb_capability(
            &capabilities,
            RGB_CAP_RESTORE_MODE,
            "effect restore for failed frame rollback",
        )?;
        rgb_bridge_set_effect_on_device(&device, capabilities.live_effect_id)?;
    }

    let upload_result = (|| {
        let chunk_bytes = usize::from(capabilities.chunk_bytes);
        for (chunk, data) in frame.chunks(chunk_bytes).enumerate() {
            let chunk = u8::try_from(chunk)
                .map_err(|_| "RGB frame requires more than 256 chunks".to_string())?;
            let length = u8::try_from(data.len())
                .map_err(|_| "RGB frame chunk exceeds the protocol".to_string())?;
            let mut payload = Vec::with_capacity(data.len() + 2);
            payload.push(chunk);
            payload.push(length);
            payload.extend_from_slice(data);
            let response = rgb_bridge_exchange(&device, RGB_CMD_SET_FRAME_CHUNK, &payload)?;
            if response[2] != chunk || response[3] != length {
                return Err(format!(
                    "RGB bridge acknowledged the wrong frame chunk {chunk}"
                ));
            }
        }
        Ok(())
    })();

    upload_result.map_err(|error| {
        // A partial frame is not published. Only undo the live transition we
        // introduced; if the device was already live, keep its prior visible
        // frame/mode instead of unexpectedly restoring another effect.
        let rollback = if plan.restore_effect_on_failure {
            rgb_bridge_exchange(&device, RGB_CMD_RESTORE_EFFECT, &[]).map(|_| ())
        } else {
            Ok(())
        };
        rgb_bridge_rollback_error(error, rollback)
    })
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
    expected_serial_number: Option<String>,
    state: State<'_, KbheTransportState>,
) -> Result<KbheConnectionState, String> {
    // Fast rejection avoids even opening a competing HID handle. The second
    // check under `state.active` below closes the check/open race.
    ensure_transport_not_flashing()?;
    let devices = enumerate_kbhe_devices()?;
    let matched = devices
        .into_iter()
        .find(|device| device.path == path)
        .ok_or_else(|| format!("KBHE device path not found: {path}"))?;

    let expected_serial_number = expected_serial_number
        .map(|serial| {
            let serial = serial.trim().to_string();
            if serial.is_empty() {
                Err("expected keyboard serial number must not be empty".to_string())
            } else {
                Ok(serial)
            }
        })
        .transpose()?;
    if let Some(expected_serial) = expected_serial_number.as_deref() {
        if matched.serial_number.as_deref().map(str::trim) != Some(expected_serial) {
            return Err(format!(
                "KBHE device path no longer belongs to keyboard serial {expected_serial}"
            ));
        }
    }

    let device = open_device_by_path(&matched.path)?;
    if let Some(expected_serial) = expected_serial_number.as_deref() {
        verify_open_device_serial(&device, expected_serial)?;
    }
    let mut active = lock_active(&state)?;
    ensure_transport_not_flashing()?;
    *active = Some(ActiveConnection {
        device,
        path: matched.path.clone(),
        pid: matched.pid,
        kind: matched.kind,
    });

    Ok(active_connection_state(active.as_ref()))
}

#[tauri::command]
pub fn kbhe_disconnect(
    state: State<'_, KbheTransportState>,
) -> Result<KbheConnectionState, String> {
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
    ensure_transport_not_flashing()?;
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
    if report.len() != KBHE_PACKET_SIZE + 1 {
        return Err(format!(
            "RAW HID output report has {} bytes; expected {}",
            report.len(),
            KBHE_PACKET_SIZE + 1
        ));
    }
    let mut active = lock_active(&state)?;
    ensure_transport_not_flashing()?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    let written = connection
        .device
        .write(&report)
        .map_err(|error| error.to_string())?;
    if written != report.len() {
        return Err(format!(
            "short RAW HID write: wrote {written} of {} bytes",
            report.len()
        ));
    }
    Ok(written)
}

#[tauri::command]
pub fn kbhe_read_report(
    timeout_ms: u64,
    state: State<'_, KbheTransportState>,
) -> Result<Vec<u8>, String> {
    let mut active = lock_active(&state)?;
    ensure_transport_not_flashing()?;
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
    ensure_transport_not_flashing()?;
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
    ensure_transport_not_flashing()?;
    let connection = active
        .as_mut()
        .ok_or_else(|| "no KBHE HID device is currently connected".to_string())?;

    let mut states = vec![0u8; KBHE_KEY_COUNT];
    let mut distances = vec![0u8; KBHE_KEY_COUNT];
    let mut distances_01mm = vec![0u16; KBHE_KEY_COUNT];
    let mut distances_mm = vec![0f32; KBHE_KEY_COUNT];

    let mut next_index = 0usize;
    while next_index < KBHE_KEY_COUNT {
        let response =
            send_command_on_active(connection, CMD_GET_KEY_STATES, &[0, next_index as u8], 150)?
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
            let value_01mm =
                u16::from(response[offset + 2]) | (u16::from(response[offset + 3]) << 8);

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
    KeyProbe {
        code: "Backquote",
        scan_code: 0x29,
    },
    KeyProbe {
        code: "Digit1",
        scan_code: 0x02,
    },
    KeyProbe {
        code: "Digit2",
        scan_code: 0x03,
    },
    KeyProbe {
        code: "Digit3",
        scan_code: 0x04,
    },
    KeyProbe {
        code: "Digit4",
        scan_code: 0x05,
    },
    KeyProbe {
        code: "Digit5",
        scan_code: 0x06,
    },
    KeyProbe {
        code: "Digit6",
        scan_code: 0x07,
    },
    KeyProbe {
        code: "Digit7",
        scan_code: 0x08,
    },
    KeyProbe {
        code: "Digit8",
        scan_code: 0x09,
    },
    KeyProbe {
        code: "Digit9",
        scan_code: 0x0A,
    },
    KeyProbe {
        code: "Digit0",
        scan_code: 0x0B,
    },
    KeyProbe {
        code: "Minus",
        scan_code: 0x0C,
    },
    KeyProbe {
        code: "Equal",
        scan_code: 0x0D,
    },
    KeyProbe {
        code: "KeyQ",
        scan_code: 0x10,
    },
    KeyProbe {
        code: "KeyW",
        scan_code: 0x11,
    },
    KeyProbe {
        code: "KeyE",
        scan_code: 0x12,
    },
    KeyProbe {
        code: "KeyR",
        scan_code: 0x13,
    },
    KeyProbe {
        code: "KeyT",
        scan_code: 0x14,
    },
    KeyProbe {
        code: "KeyY",
        scan_code: 0x15,
    },
    KeyProbe {
        code: "KeyU",
        scan_code: 0x16,
    },
    KeyProbe {
        code: "KeyI",
        scan_code: 0x17,
    },
    KeyProbe {
        code: "KeyO",
        scan_code: 0x18,
    },
    KeyProbe {
        code: "KeyP",
        scan_code: 0x19,
    },
    KeyProbe {
        code: "BracketLeft",
        scan_code: 0x1A,
    },
    KeyProbe {
        code: "BracketRight",
        scan_code: 0x1B,
    },
    KeyProbe {
        code: "IntlHash",
        scan_code: 0x2B,
    },
    KeyProbe {
        code: "Backslash",
        scan_code: 0x2B,
    },
    KeyProbe {
        code: "KeyA",
        scan_code: 0x1E,
    },
    KeyProbe {
        code: "KeyS",
        scan_code: 0x1F,
    },
    KeyProbe {
        code: "KeyD",
        scan_code: 0x20,
    },
    KeyProbe {
        code: "KeyF",
        scan_code: 0x21,
    },
    KeyProbe {
        code: "KeyG",
        scan_code: 0x22,
    },
    KeyProbe {
        code: "KeyH",
        scan_code: 0x23,
    },
    KeyProbe {
        code: "KeyJ",
        scan_code: 0x24,
    },
    KeyProbe {
        code: "KeyK",
        scan_code: 0x25,
    },
    KeyProbe {
        code: "KeyL",
        scan_code: 0x26,
    },
    KeyProbe {
        code: "Semicolon",
        scan_code: 0x27,
    },
    KeyProbe {
        code: "Quote",
        scan_code: 0x28,
    },
    KeyProbe {
        code: "IntlBackslash",
        scan_code: 0x56,
    },
    KeyProbe {
        code: "KeyZ",
        scan_code: 0x2C,
    },
    KeyProbe {
        code: "KeyX",
        scan_code: 0x2D,
    },
    KeyProbe {
        code: "KeyC",
        scan_code: 0x2E,
    },
    KeyProbe {
        code: "KeyV",
        scan_code: 0x2F,
    },
    KeyProbe {
        code: "KeyB",
        scan_code: 0x30,
    },
    KeyProbe {
        code: "KeyN",
        scan_code: 0x31,
    },
    KeyProbe {
        code: "KeyM",
        scan_code: 0x32,
    },
    KeyProbe {
        code: "Comma",
        scan_code: 0x33,
    },
    KeyProbe {
        code: "Period",
        scan_code: 0x34,
    },
    KeyProbe {
        code: "Slash",
        scan_code: 0x35,
    },
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
            let flush = unsafe {
                ToUnicodeEx(vk, scan_code, &empty_state, &mut flush_buffer, 0, Some(hkl))
            };
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
const UPDATER_CMD_AUTH: u8 = 0x07;
const UPDATER_CMD_BOOTLOADER_INFO: u8 = 0x08;
const UPDATER_STATUS_OK: u8 = 0x00;
const UPDATER_STATUS_INVALID_COMMAND: u8 = 0x02;
const APP_CMD_ENTER_BOOTLOADER: u8 = 0x02;
const DATA_CHUNK_SIZE: usize = 56;
const FIRMWARE_SIGNATURE_SIZE: usize = 64;
const UPDATER_TIMEOUT_MIN_MS: u64 = 1_000;
const UPDATER_TIMEOUT_MAX_MS: u64 = 30_000;
const UPDATER_RETRIES_MIN: u32 = 1;
const UPDATER_RETRIES_MAX: u32 = 20;
const UPDATER_BEGIN_MIN_TIMEOUT_MS: u64 = 6_000;
/// Device presence poll interval (matches Python DEVICE_POLL_DELAY_S = 0.02)
const DEVICE_POLL_MS: u64 = 20;

fn updater_status_name(status: u8) -> &'static str {
    match status {
        0x00 => "OK",
        0x01 => "ERROR",
        0x02 => "INVALID_COMMAND",
        0x03 => "INVALID_PARAMETER",
        0x04 => "INVALID_STATE",
        0x05 => "VERIFY_FAILED",
        0x06 => "INVALID_IMAGE",
        0x07 => "AUTH_REQUIRED",
        0x08 => "AUTH_FAILED",
        0x09 => "ROLLBACK_REJECTED",
        0x0a => "STORAGE_ERROR",
        _ => "UNKNOWN_STATUS",
    }
}

fn updater_status_error(operation: &str, status: u8) -> String {
    format!(
        "{operation} failed: {} (0x{status:02X})",
        updater_status_name(status)
    )
}

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
    if payload.len() > DATA_CHUNK_SIZE {
        return Err(format!(
            "updater payload has {} bytes; maximum is {DATA_CHUNK_SIZE}",
            payload.len()
        ));
    }
    let write_buf = build_updater_write_buf(command, sequence, offset, payload);

    for attempt in 0..retries {
        // Discard bounded stale reports from an earlier exchange before
        // retransmitting this exact idempotent updater packet.
        let mut drain = [0u8; KBHE_PACKET_SIZE];
        for _ in 0..8 {
            match device.read_timeout(&mut drain, 1) {
                Ok(0) | Err(_) => break,
                Ok(_) => {}
            }
        }
        let written = device.write(&write_buf).map_err(|e| e.to_string())?;
        if written != write_buf.len() {
            return Err(format!(
                "short updater write: wrote {written} of {} bytes",
                write_buf.len()
            ));
        }

        let deadline = Instant::now() + Duration::from_millis(timeout_ms);
        let mut rbuf = [0u8; KBHE_PACKET_SIZE];

        while Instant::now() < deadline {
            let remaining = deadline.saturating_duration_since(Instant::now());
            let t = i32::try_from(remaining.as_millis().max(1)).unwrap_or(i32::MAX);
            let n = device
                .read_timeout(&mut rbuf, t)
                .map_err(|e| e.to_string())?;
            if n == KBHE_PACKET_SIZE && rbuf[0] == command && rbuf[1] == sequence {
                let status = rbuf[2];
                let length = rbuf[3] as usize;
                if length > DATA_CHUNK_SIZE || 8 + length > n {
                    return Err("malformed updater response framing".to_string());
                }
                let resp_offset = u32::from_le_bytes([rbuf[4], rbuf[5], rbuf[6], rbuf[7]]);
                let payload_end = 8 + length;
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

fn find_updater_device(expected_serial: &str) -> Result<Option<KbheHidDeviceInfo>, String> {
    find_flash_device(KbheDeviceKind::Updater, expected_serial)
}

fn find_runtime_device(expected_serial: &str) -> Result<Option<KbheHidDeviceInfo>, String> {
    find_flash_device(KbheDeviceKind::Runtime, expected_serial)
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

#[derive(Debug, Clone, Copy, Default, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u8,
}

#[tauri::command]
pub async fn kbhe_flash_firmware(
    firmware_path: String,
    firmware_signature_path: String,
    migration_firmware_path: Option<String>,
    migration_firmware_signature_path: Option<String>,
    bootloader_refresh_path: Option<String>,
    bootloader_refresh_signature_path: Option<String>,
    settings_backup_complete: bool,
    app_only_recovery: bool,
    firmware_version: FirmwareVersion,
    expected_serial_number: String,
    timeout_ms: u64,
    retries: u32,
    app: AppHandle,
    state: State<'_, KbheTransportState>,
) -> Result<(), String> {
    if !(UPDATER_TIMEOUT_MIN_MS..=UPDATER_TIMEOUT_MAX_MS).contains(&timeout_ms) {
        return Err(format!(
            "updater timeout must be {UPDATER_TIMEOUT_MIN_MS}..={UPDATER_TIMEOUT_MAX_MS} ms"
        ));
    }
    if !(UPDATER_RETRIES_MIN..=UPDATER_RETRIES_MAX).contains(&retries) {
        return Err(format!(
            "updater retries must be {UPDATER_RETRIES_MIN}..={UPDATER_RETRIES_MAX}"
        ));
    }
    let expected_serial_number = expected_serial_number.trim().to_string();
    if expected_serial_number.is_empty() {
        return Err(
            "firmware flashing requires the non-empty serial number of the connected keyboard"
                .to_string(),
        );
    }
    let flash_guard =
        ExclusiveOperationGuard::acquire(&FIRMWARE_FLASH_ACTIVE, "firmware flashing")?;
    {
        let mut active = lock_active(&state)?;
        *active = None;
    }

    tauri::async_runtime::spawn_blocking(move || {
        let _flash_guard = flash_guard;
        kbhe_flash_firmware_blocking(
            firmware_path,
            firmware_signature_path,
            migration_firmware_path,
            migration_firmware_signature_path,
            bootloader_refresh_path,
            bootloader_refresh_signature_path,
            settings_backup_complete,
            app_only_recovery,
            firmware_version,
            expected_serial_number,
            timeout_ms,
            retries,
            app,
        )
    })
    .await
    .map_err(|error| format!("firmware flash worker failed: {error}"))?
}

fn existing_application_recovery_command(hello: UpdaterHello) -> Result<u8, String> {
    negotiate_flash_protocol(hello, &FirmwareArtifact::Application)?;
    if hello.flags & UPDATER_FLAG_APP_VALID == 0 {
        return Err(
            "UPDATER_APP_INVALID: updater v3 has no valid installed application to boot; no destructive command was sent"
                .to_string(),
        );
    }
    Ok(UPDATER_CMD_BOOT)
}

#[tauri::command]
pub async fn kbhe_boot_existing_application(
    expected_serial_number: String,
    timeout_ms: u64,
    retries: u32,
    app: AppHandle,
    state: State<'_, KbheTransportState>,
) -> Result<(), String> {
    if !(UPDATER_TIMEOUT_MIN_MS..=UPDATER_TIMEOUT_MAX_MS).contains(&timeout_ms) {
        return Err(format!(
            "updater timeout must be {UPDATER_TIMEOUT_MIN_MS}..={UPDATER_TIMEOUT_MAX_MS} ms"
        ));
    }
    if !(UPDATER_RETRIES_MIN..=UPDATER_RETRIES_MAX).contains(&retries) {
        return Err(format!(
            "updater retries must be {UPDATER_RETRIES_MIN}..={UPDATER_RETRIES_MAX}"
        ));
    }
    let expected_serial_number = expected_serial_number.trim().to_string();
    if expected_serial_number.is_empty() {
        return Err("boot recovery requires a non-empty keyboard serial number".to_string());
    }
    let operation_guard =
        ExclusiveOperationGuard::acquire(&FIRMWARE_FLASH_ACTIVE, "application boot recovery")?;
    {
        let mut active = lock_active(&state)?;
        *active = None;
    }

    tauri::async_runtime::spawn_blocking(move || {
        let _operation_guard = operation_guard;
        let devices = enumerate_kbhe_devices()?;
        let (runtime, updater) = resolve_flash_target_snapshot(&devices, &expected_serial_number)?;
        if runtime.is_some() {
            return Err(
                "UPDATER_BOOT_RECOVERY_NOT_NEEDED: keyboard runtime is already reachable"
                    .to_string(),
            );
        }
        let updater = updater.ok_or_else(|| {
            format!(
                "updater for keyboard serial {expected_serial_number} was not found unambiguously"
            )
        })?;
        let device = open_device_by_path(&updater.path)?;
        verify_open_device_serial(&device, &expected_serial_number)?;
        let hello = read_updater_hello(&device, &app, 0, timeout_ms, retries)?;
        let command = existing_application_recovery_command(hello)?;
        let response = updater_transact(
            &device,
            command,
            0xB1,
            0,
            &[],
            retries.min(3),
            timeout_ms.min(2_000),
        )?;
        if response.status != UPDATER_STATUS_OK {
            return Err(updater_status_error("BOOT", response.status));
        }
        Ok(())
    })
    .await
    .map_err(|error| format!("application boot recovery worker failed: {error}"))?
}

#[tauri::command]
pub fn kbhe_read_firmware_signature(firmware_path: String) -> Result<Vec<u8>, String> {
    read_sibling_firmware_signature(&firmware_path)
}

fn reject_link_or_reparse_point(path: &Path, label: &str) -> Result<std::fs::Metadata, String> {
    let metadata = std::fs::symlink_metadata(path)
        .map_err(|error| format!("failed to inspect {label}: {error}"))?;
    if metadata.file_type().is_symlink() {
        return Err(format!("refusing symbolic link for {label}"));
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;
        if metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            return Err(format!("refusing Windows reparse point for {label}"));
        }
    }
    if !metadata.is_file() {
        return Err(format!("{label} path is not a regular file"));
    }
    Ok(metadata)
}

fn read_sibling_firmware_signature(firmware_path: &str) -> Result<Vec<u8>, String> {
    let firmware = Path::new(firmware_path);
    if !firmware.is_absolute()
        || !firmware
            .extension()
            .and_then(|extension| extension.to_str())
            .is_some_and(|extension| extension.eq_ignore_ascii_case("bin"))
    {
        return Err("firmware path must be an absolute .bin file".to_string());
    }

    let firmware_metadata = reject_link_or_reparse_point(firmware, "firmware")?;
    if firmware_metadata.len() == 0
        || firmware_metadata.len() > u64::from(UPDATER_V2_APP_MAX_IMAGE_SIZE)
    {
        return Err(format!(
            "firmware has {} bytes; expected 1..={UPDATER_V2_APP_MAX_IMAGE_SIZE}",
            firmware_metadata.len()
        ));
    }

    let mut signature_name = firmware.as_os_str().to_os_string();
    signature_name.push(".sig");
    let signature = PathBuf::from(signature_name);
    let signature_metadata = reject_link_or_reparse_point(&signature, "firmware signature")?;
    if signature_metadata.len() != FIRMWARE_SIGNATURE_SIZE as u64 {
        return Err(format!(
            "firmware signature has {} bytes; expected {FIRMWARE_SIGNATURE_SIZE}",
            signature_metadata.len()
        ));
    }

    let signature_path = signature
        .to_str()
        .ok_or_else(|| "firmware signature path is not valid Unicode".to_string())?;
    let bytes = read_bounded_file(
        signature_path,
        "firmware signature",
        FIRMWARE_SIGNATURE_SIZE as u64,
    )?;
    if bytes.len() != FIRMWARE_SIGNATURE_SIZE {
        return Err(format!(
            "firmware signature has {} bytes; expected {FIRMWARE_SIGNATURE_SIZE}",
            bytes.len()
        ));
    }
    Ok(bytes)
}

fn read_bounded_file(path: &str, label: &str, maximum_size: u64) -> Result<Vec<u8>, String> {
    let file = File::open(path).map_err(|error| format!("failed to open {label}: {error}"))?;
    let metadata = file
        .metadata()
        .map_err(|error| format!("failed to inspect {label}: {error}"))?;
    if !metadata.is_file() {
        return Err(format!("{label} path is not a regular file"));
    }
    if metadata.len() == 0 || metadata.len() > maximum_size {
        return Err(format!(
            "{label} has {} bytes; expected 1..={maximum_size}",
            metadata.len()
        ));
    }

    let capacity = usize::try_from(metadata.len())
        .map_err(|_| format!("{label} is too large for this platform"))?;
    let mut bytes = Vec::with_capacity(capacity);
    file.take(maximum_size + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| format!("failed to read {label}: {error}"))?;
    if bytes.len() as u64 != metadata.len() {
        return Err(format!(
            "{label} changed while it was being read (expected {} bytes, read {})",
            metadata.len(),
            bytes.len()
        ));
    }
    Ok(bytes)
}

struct PreparedFlashArtifact {
    firmware: Vec<u8>,
    padded: Vec<u8>,
    signature: Vec<u8>,
    artifact: FirmwareArtifact,
    image_crc32: u32,
    version: FirmwareVersion,
    total: u32,
}

fn prepare_flash_artifact(
    firmware_path: &str,
    signature_path: &str,
    version: FirmwareVersion,
    label: &str,
) -> Result<PreparedFlashArtifact, String> {
    let firmware = read_bounded_file(
        firmware_path,
        label,
        u64::from(UPDATER_V2_APP_MAX_IMAGE_SIZE),
    )?;
    let signature = read_bounded_file(
        signature_path,
        &format!("{label} signature"),
        FIRMWARE_SIGNATURE_SIZE as u64,
    )?;
    if signature.len() != FIRMWARE_SIGNATURE_SIZE {
        return Err(format!(
            "{label} signature has {} bytes; expected {FIRMWARE_SIGNATURE_SIZE}",
            signature.len()
        ));
    }
    let artifact = inspect_firmware_artifact(
        &firmware,
        [version.major, version.minor, version.patch],
        &signature,
    )?;
    let aligned_len = (firmware.len() + 3) & !3;
    let mut padded = firmware.clone();
    padded.resize(aligned_len, 0xFF);
    let image_crc32 = crc32_compute(&firmware);
    let total = u32::try_from(firmware.len())
        .map_err(|_| format!("{label} length exceeds the updater protocol"))?;
    Ok(PreparedFlashArtifact {
        firmware,
        padded,
        signature,
        artifact,
        image_crc32,
        version,
        total,
    })
}

fn validate_firmware_carrier_name(
    firmware_path: &str,
    firmware_version: FirmwareVersion,
) -> Result<(), String> {
    let version = SemverVersion::new(
        u64::from(firmware_version.major),
        u64::from(firmware_version.minor),
        u64::from(firmware_version.patch),
    );
    let expected = firmware_asset_name_for_version(&version);
    let observed = Path::new(firmware_path)
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or_else(|| "firmware carrier path has no valid Unicode file name".to_string())?;
    if observed != expected {
        return Err(format!(
            "UPDATER_CARRIER_ROLE_MISMATCH: firmware {version} must use the exact signed carrier name {expected}; selected {observed}"
        ));
    }
    Ok(())
}

fn discover_sibling_migration_assets(
    firmware_path: &str,
) -> Result<Option<(String, String)>, String> {
    let firmware_path = Path::new(firmware_path);
    if !firmware_path
        .file_name()
        .and_then(|name| name.to_str())
        .is_some_and(is_firmware_carrier_name)
    {
        return Ok(None);
    }
    let Some(parent) = firmware_path.parent() else {
        return Ok(None);
    };
    let migration = parent.join("kbhe-updater-v2-to-v3.bin");
    let migration_signature = parent.join("kbhe-updater-v2-to-v3.bin.sig");
    let migration_present = migration.is_file();
    let signature_present = migration_signature.is_file();
    if migration_present != signature_present {
        return Err(
            "automatic updater migration found only one of kbhe-updater-v2-to-v3.bin and its detached signature"
                .to_string(),
        );
    }
    if !migration_present {
        return Ok(None);
    }
    let migration = migration
        .to_str()
        .ok_or_else(|| "updater migration path is not valid Unicode".to_string())?;
    let migration_signature = migration_signature
        .to_str()
        .ok_or_else(|| "updater migration signature path is not valid Unicode".to_string())?;
    Ok(Some((
        migration.to_string(),
        migration_signature.to_string(),
    )))
}

fn discover_sibling_bootloader_refresh_assets(
    firmware_path: &str,
) -> Result<Option<(String, String)>, String> {
    let firmware_path = Path::new(firmware_path);
    if !firmware_path
        .file_name()
        .and_then(|name| name.to_str())
        .is_some_and(is_firmware_carrier_name)
    {
        return Ok(None);
    }
    let Some(parent) = firmware_path.parent() else {
        return Ok(None);
    };
    let refresh = parent.join("kbhe-updater-v3-refresh.bin");
    let refresh_signature = parent.join("kbhe-updater-v3-refresh.bin.sig");
    let refresh_present = refresh.is_file();
    let signature_present = refresh_signature.is_file();
    if refresh_present != signature_present {
        return Err(
            "automatic updater refresh found only one of kbhe-updater-v3-refresh.bin and its detached signature"
                .to_string(),
        );
    }
    if !refresh_present {
        return Ok(None);
    }
    Ok(Some((
        refresh
            .to_str()
            .ok_or_else(|| "updater refresh path is not valid Unicode".to_string())?
            .to_string(),
        refresh_signature
            .to_str()
            .ok_or_else(|| "updater refresh signature path is not valid Unicode".to_string())?
            .to_string(),
    )))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DestructiveUpdaterOperation {
    V2ToV3Migration,
    V3Refresh,
}

fn run_bootloader_change_with_backup<T>(
    operation: DestructiveUpdaterOperation,
    settings_backup_complete: bool,
    change: impl FnOnce() -> Result<T, String>,
) -> Result<T, String> {
    if !settings_backup_complete {
        return Err(match operation {
            DestructiveUpdaterOperation::V2ToV3Migration =>
                "SETTINGS_BACKUP_REQUIRED: updater v2-to-v3 migration was refused before BEGIN because no verified schema-3 settings backup exists. Return to runtime and retry so the keyboard name, calibration and all profile settings are captured first; use the documented ROM-DFU recovery if runtime is unavailable."
                    .to_string(),
            DestructiveUpdaterOperation::V3Refresh => format!(
                "SETTINGS_BACKUP_REQUIRED: resident updater refresh was refused before BEGIN because no verified schema-3 settings backup exists. If runtime cannot boot, choose Recover runtime only: Configurator will fetch the exact installed-version carrier and disable support-asset discovery. Reconnect runtime, capture schema 3, then retry the newer refresh."
            ),
        });
    }
    change()
}

fn support_artifact_discovery_allowed(app_only_recovery: bool) -> bool {
    !app_only_recovery
}

fn validate_app_only_recovery_protocol(
    app_only_recovery: bool,
    updater_protocol: u16,
) -> Result<(), String> {
    if app_only_recovery && updater_protocol != UPDATER_PROTOCOL_V3 {
        return Err(format!(
            "UPDATER_APP_ONLY_RECOVERY_UNSUPPORTED: app-only recovery requires updater v3; observed protocol 0x{updater_protocol:04X}. No BEGIN was sent."
        ));
    }
    Ok(())
}

fn validate_app_only_recovery_version(
    hello: UpdaterHello,
    selected: FirmwareVersion,
) -> Result<(), String> {
    let selected = [selected.major, selected.minor, selected.patch];
    if hello.installed_version == [0, 0, 0] || hello.installed_version != selected {
        return Err(format!(
            "UPDATER_APP_ONLY_RECOVERY_VERSION_MISMATCH: app-only recovery must use the exact installed application version {}.{}.{}; selected {}.{}.{}. No BEGIN was sent.",
            hello.installed_version[0],
            hello.installed_version[1],
            hello.installed_version[2],
            selected[0],
            selected[1],
            selected[2],
        ));
    }
    Ok(())
}

fn kbhe_flash_firmware_blocking(
    firmware_path: String,
    firmware_signature_path: String,
    migration_firmware_path: Option<String>,
    migration_firmware_signature_path: Option<String>,
    bootloader_refresh_path: Option<String>,
    bootloader_refresh_signature_path: Option<String>,
    settings_backup_complete: bool,
    app_only_recovery: bool,
    firmware_version: FirmwareVersion,
    expected_serial_number: String,
    timeout_ms: u64,
    retries: u32,
    app: AppHandle,
) -> Result<(), String> {
    validate_firmware_carrier_name(&firmware_path, firmware_version)?;
    let primary = prepare_flash_artifact(
        &firmware_path,
        &firmware_signature_path,
        firmware_version,
        "firmware",
    )?;
    if app_only_recovery
        && (migration_firmware_path.is_some()
            || migration_firmware_signature_path.is_some()
            || bootloader_refresh_path.is_some()
            || bootloader_refresh_signature_path.is_some())
    {
        return Err(
            "UPDATER_APP_ONLY_RECOVERY_INVALID: app-only recovery cannot accept bootloader migration or refresh inputs"
                .to_string(),
        );
    }
    let (migration_firmware_path, migration_firmware_signature_path) =
        match (migration_firmware_path, migration_firmware_signature_path) {
            (None, None) if !support_artifact_discovery_allowed(app_only_recovery) => (None, None),
            (None, None) => match discover_sibling_migration_assets(&firmware_path)? {
                Some((path, signature_path)) => (Some(path), Some(signature_path)),
                None => (None, None),
            },
            pair => pair,
        };
    let migration = match (
        migration_firmware_path.as_deref(),
        migration_firmware_signature_path.as_deref(),
    ) {
        (None, None) => None,
        (Some(path), Some(signature_path)) => {
            let candidate = prepare_flash_artifact(
                path,
                signature_path,
                firmware_version,
                "updater migration package",
            )?;
            if !matches!(candidate.artifact, FirmwareArtifact::V2ToV3Migration(_)) {
                return Err(
                    "UPDATER_ARTIFACT_MISMATCH: automatic migration input is not the signed kbhe-updater-v2-to-v3 package"
                        .to_string(),
                );
            }
            Some(candidate)
        }
        _ => {
            return Err(
                "automatic updater migration requires both the package and its detached signature"
                    .to_string(),
            )
        }
    };
    let (bootloader_refresh_path, bootloader_refresh_signature_path) =
        match (bootloader_refresh_path, bootloader_refresh_signature_path) {
            (None, None) if !support_artifact_discovery_allowed(app_only_recovery) => (None, None),
            (None, None) => match discover_sibling_bootloader_refresh_assets(&firmware_path)? {
                Some((path, signature_path)) => (Some(path), Some(signature_path)),
                None => (None, None),
            },
            pair => pair,
        };
    let bootloader_refresh = match (
        bootloader_refresh_path.as_deref(),
        bootloader_refresh_signature_path.as_deref(),
    ) {
        (None, None) => None,
        (Some(path), Some(signature_path)) => {
            let candidate = prepare_flash_artifact(
                path,
                signature_path,
                firmware_version,
                "updater v3 refresh image",
            )?;
            if !matches!(candidate.artifact, FirmwareArtifact::V3BootloaderRefresh(_)) {
                return Err(
                    "UPDATER_ARTIFACT_MISMATCH: automatic v3 refresh input is not the signed kbhe-updater-v3-refresh inner image"
                        .to_string(),
                );
            }
            Some(candidate)
        }
        _ => return Err(
            "automatic updater v3 refresh requires both the inner image and its detached signature"
                .to_string(),
        ),
    };
    if matches!(primary.artifact, FirmwareArtifact::V3BootloaderRefresh(_)) {
        return Err(
            format!(
                "UPDATER_ARTIFACT_MISMATCH: kbhe-updater-v3-refresh.bin is a support image, not final firmware; select the matching signed {REFRESH_FIRMWARE_ASSET_NAME}"
            )
        );
    }
    if (migration.is_some() || bootloader_refresh.is_some())
        && !matches!(primary.artifact, FirmwareArtifact::Application)
    {
        return Err(
            "UPDATER_ARTIFACT_MISMATCH: automatic updater migration/refresh requires the normal signed application as the final image"
                .to_string(),
        );
    }

    emit_flash_progress(&app, "connecting", 0, primary.total);

    // Resolve one physical keyboard by its stable STM32 UID before sending any
    // device command. Runtime and updater expose the same USB serial number.
    let devices = enumerate_kbhe_devices()?;
    let (runtime, updater) = resolve_flash_target_snapshot(&devices, &expected_serial_number)?;

    if app_only_recovery && updater.is_none() {
        return Err(
            "UPDATER_APP_ONLY_RECOVERY_NOT_NEEDED: app-only recovery is available only while this keyboard already enumerates in updater mode; runtime is reachable, so capture schema 3 and run the normal update"
                .to_string(),
        );
    }

    // Request bootloader from the selected runtime device if it is not already
    // present in updater mode.
    if updater.is_none() {
        // Open a fresh connection to the runtime device and send ENTER_BOOTLOADER.
        // This works whether or not the TypeScript side already disconnected — we
        // always enumerate from scratch so we're not relying on state.active.
        let runtime_info = runtime.ok_or_else(|| {
            format!(
                "keyboard serial {expected_serial_number} was not found in runtime or updater mode"
            )
        })?;
        let runtime_device = open_device_by_path(&runtime_info.path)
            .map_err(|error| format!("failed to open runtime keyboard: {error}"))?;
        verify_open_device_serial(&runtime_device, &expected_serial_number)?;
        let mut report = [0u8; KBHE_PACKET_SIZE + 1];
        report[1] = APP_CMD_ENTER_BOOTLOADER;
        let written = runtime_device
            .write(&report)
            .map_err(|error| format!("failed to request bootloader mode: {error}"))?;
        if written != report.len() {
            return Err(format!(
                "short bootloader request: wrote {written} of {} bytes",
                report.len()
            ));
        }
        let mut tmp = [0u8; KBHE_PACKET_SIZE];
        let _ = runtime_device.read_timeout(&mut tmp, 500);
        // runtime_device dropped here — HID handle released before the device
        // re-enumerates as updater with the same serial number.
        drop(runtime_device);

        // Wait only for the selected runtime device to disconnect. Other KBHE
        // keyboards must not influence this transition.
        wait_for_device_absent(
            || find_runtime_device(&expected_serial_number),
            5_000,
        )
        .map_err(|error| {
            format!(
                "runtime keyboard serial {expected_serial_number} did not disconnect unambiguously: {error}"
            )
        })?;
    }

    // Wait for the same physical keyboard in updater mode.
    let updater_info = wait_for_device_kind(|| find_updater_device(&expected_serial_number), 8_000)
        .map_err(|error| {
            format!("updater for keyboard serial {expected_serial_number} was not found: {error}")
        })?;

    let device = open_device_by_path(&updater_info.path)?;
    verify_open_device_serial(&device, &expected_serial_number)?;

    // HELLO is the only command sent before the exact protocol, geometry and
    // security contract have been accepted. Unknown/malformed responders are
    // left untouched instead of guessing that ABORT/BOOT still have v2/v3
    // semantics.
    let initial_hello = read_updater_hello(&device, &app, primary.total, timeout_ms, retries)?;
    validate_app_only_recovery_protocol(app_only_recovery, initial_hello.protocol_version)?;
    if app_only_recovery {
        validate_app_only_recovery_version(initial_hello, firmware_version)?;
        negotiate_flash_protocol_with_cleanup(
            &device,
            initial_hello,
            &primary.artifact,
            retries,
            timeout_ms,
        )?;
        if initial_hello.flags & UPDATER_FLAG_APP_VALID != 0 {
            emit_flash_progress(&app, "appOnlyBoot", 0, primary.total);
            let boot = updater_transact(
                &device,
                UPDATER_CMD_BOOT,
                0xB1,
                0,
                &[],
                retries.min(3),
                timeout_ms.min(2_000),
            )?;
            if boot.status != UPDATER_STATUS_OK {
                return Err(updater_status_error("BOOT", boot.status));
            }
            return Ok(());
        }
    }

    if initial_hello.protocol_version == UPDATER_PROTOCOL_V2 && migration.is_some() {
        let migration = migration.as_ref().expect("migration was checked");
        let FirmwareArtifact::V2ToV3Migration(migration_metadata) = &migration.artifact else {
            unreachable!("migration artifact was checked during preparation");
        };
        run_bootloader_change_with_backup(
            DestructiveUpdaterOperation::V2ToV3Migration,
            settings_backup_complete,
            || {
                let protocol = negotiate_flash_protocol_with_cleanup(
                    &device,
                    initial_hello,
                    &migration.artifact,
                    retries,
                    timeout_ms,
                )?;
                if let Err(error) = flash_after_hello(
                    &device,
                    initial_hello,
                    protocol,
                    migration,
                    &app,
                    timeout_ms,
                    retries,
                ) {
                    cleanup_updater_session(&device, retries, timeout_ms);
                    return Err(error);
                }
                Ok(())
            },
        )?;
        drop(device);

        emit_flash_progress(&app, "migration", migration.total, migration.total);
        let (v3_device, v3_hello) = wait_for_updater_protocol(
            &expected_serial_number,
            UPDATER_PROTOCOL_V3,
            90_000,
            &app,
            primary.total,
            timeout_ms,
            retries,
        )
        .map_err(|error| {
            format!(
                "UPDATER_MIGRATION_INCOMPLETE: updater v3 for keyboard serial {expected_serial_number} did not become ready after the migrator ran: {error}. Keep the keyboard connected and use the documented ROM-DFU recovery if it no longer enumerates."
            )
        })?;
        let installed_bootloader =
            read_updater_bootloader_info(&v3_device, timeout_ms.min(1_000), retries.min(2))
                .map_err(|error| format!("UPDATER_MIGRATION_INCOMPLETE: {error}"))?;
        verify_refreshed_bootloader(installed_bootloader, migration_metadata)
            .map_err(|error| format!("UPDATER_MIGRATION_INCOMPLETE: {error}"))?;
        let protocol = negotiate_flash_protocol_with_cleanup(
            &v3_device,
            v3_hello,
            &primary.artifact,
            retries,
            timeout_ms,
        )
        .map_err(|error| format!("UPDATER_MIGRATION_INCOMPLETE: {error}"))?;
        emit_flash_progress(&app, "migrationReady", primary.total, primary.total);
        if let Err(error) = flash_after_hello(
            &v3_device, v3_hello, protocol, &primary, &app, timeout_ms, retries,
        ) {
            cleanup_updater_session(&v3_device, retries, timeout_ms);
            return Err(error);
        }
        drop(v3_device);
    } else if initial_hello.protocol_version == UPDATER_PROTOCOL_V3 && bootloader_refresh.is_some()
    {
        let refresh = bootloader_refresh
            .as_ref()
            .expect("bootloader refresh was checked");
        let FirmwareArtifact::V3BootloaderRefresh(refresh_metadata) = &refresh.artifact else {
            unreachable!("refresh artifact was checked during preparation");
        };
        /* Validate the final application contract before deciding whether the
         * optional resident-updater stage is needed. */
        negotiate_flash_protocol_with_cleanup(
            &device,
            initial_hello,
            &primary.artifact,
            retries,
            timeout_ms,
        )?;
        emit_flash_progress(&app, "bootloaderCheck", 0, primary.total);
        let installed_bootloader =
            read_updater_bootloader_info(&device, timeout_ms.min(1_000), retries.min(2))?;
        if bootloader_refresh_required(installed_bootloader, refresh_metadata)? {
            run_bootloader_change_with_backup(
                DestructiveUpdaterOperation::V3Refresh,
                settings_backup_complete,
                || {
                    emit_flash_progress(&app, "bootloaderRefresh", 0, refresh.total);
                    let protocol = negotiate_flash_protocol_with_cleanup(
                        &device,
                        initial_hello,
                        &refresh.artifact,
                        retries,
                        timeout_ms,
                    )?;
                    if let Err(error) = flash_after_hello(
                        &device,
                        initial_hello,
                        protocol,
                        refresh,
                        &app,
                        timeout_ms,
                        retries,
                    ) {
                        cleanup_updater_session(&device, retries, timeout_ms);
                        return Err(error);
                    }
                    Ok(())
                },
            )?;
            drop(device);

            let (refreshed_device, refreshed_hello) = wait_for_updater_protocol(
                &expected_serial_number,
                UPDATER_PROTOCOL_V3,
                90_000,
                &app,
                primary.total,
                timeout_ms,
                retries,
            )
            .map_err(|error| format!(
                "UPDATER_BOOTLOADER_REFRESH_INCOMPLETE: updater v3 for keyboard serial {expected_serial_number} did not become ready after the signed refresh: {error}. Keep the keyboard connected; the migrator resumes across power cuts."
            ))?;
            let installed_bootloader = read_updater_bootloader_info(
                &refreshed_device,
                timeout_ms.min(1_000),
                retries.min(2),
            )?;
            verify_refreshed_bootloader(installed_bootloader, refresh_metadata)?;
            let protocol = negotiate_flash_protocol_with_cleanup(
                &refreshed_device,
                refreshed_hello,
                &primary.artifact,
                retries,
                timeout_ms,
            )?;
            emit_flash_progress(&app, "migrationReady", primary.total, primary.total);
            if let Err(error) = flash_after_hello(
                &refreshed_device,
                refreshed_hello,
                protocol,
                &primary,
                &app,
                timeout_ms,
                retries,
            ) {
                cleanup_updater_session(&refreshed_device, retries, timeout_ms);
                return Err(error);
            }
            drop(refreshed_device);
        } else {
            emit_flash_progress(&app, "bootloaderCurrent", 0, primary.total);
            let protocol = negotiate_flash_protocol_with_cleanup(
                &device,
                initial_hello,
                &primary.artifact,
                retries,
                timeout_ms,
            )?;
            if let Err(error) = flash_after_hello(
                &device,
                initial_hello,
                protocol,
                &primary,
                &app,
                timeout_ms,
                retries,
            ) {
                cleanup_updater_session(&device, retries, timeout_ms);
                return Err(error);
            }
            drop(device);
        }
    } else {
        let protocol = negotiate_flash_protocol_with_cleanup(
            &device,
            initial_hello,
            &primary.artifact,
            retries,
            timeout_ms,
        )?;
        if let Err(error) = flash_after_hello(
            &device,
            initial_hello,
            protocol,
            &primary,
            &app,
            timeout_ms,
            retries,
        ) {
            cleanup_updater_session(&device, retries, timeout_ms);
            return Err(error);
        }
        drop(device);
    }

    if matches!(primary.artifact, FirmwareArtifact::Application) {
        wait_for_device_absent(|| find_updater_device(&expected_serial_number), 10_000)?;
        wait_for_device_kind(|| find_runtime_device(&expected_serial_number), 15_000).map_err(
            |error| format!(
                "application for keyboard serial {expected_serial_number} did not come back: {error}"
            ),
        )?;
    } else {
        emit_flash_progress(&app, "migration", primary.total, primary.total);
        let (migrated_device, migrated_hello) = wait_for_updater_protocol(
            &expected_serial_number,
            UPDATER_PROTOCOL_V3,
            90_000,
            &app,
            primary.total,
            timeout_ms,
            retries,
        )
        .map_err(|error| format!(
            "UPDATER_MIGRATION_INCOMPLETE: updater v3 for keyboard serial {expected_serial_number} did not become ready after the migrator ran: {error}"
        ))?;
        negotiate_flash_protocol_with_cleanup(
            &migrated_device,
            migrated_hello,
            &FirmwareArtifact::Application,
            retries,
            timeout_ms,
        )
        .map_err(|error| format!("UPDATER_MIGRATION_INCOMPLETE: {error}"))?;
        emit_flash_progress(&app, "migrationReady", primary.total, primary.total);
    }

    emit_flash_progress(&app, "done", primary.total, primary.total);
    Ok(())
}

fn cleanup_updater_session(device: &HidDevice, retries: u32, timeout_ms: u64) {
    let cleanup_retries = retries.min(2);
    let _ = updater_transact(
        device,
        UPDATER_CMD_ABORT,
        0xFF,
        0,
        &[],
        cleanup_retries,
        timeout_ms.min(1_000),
    );
    let _ = updater_transact(
        device,
        UPDATER_CMD_BOOT,
        0xFE,
        0,
        &[],
        cleanup_retries,
        timeout_ms.min(1_000),
    );
}

fn negotiate_flash_protocol_with_cleanup(
    device: &HidDevice,
    hello: UpdaterHello,
    artifact: &FirmwareArtifact,
    retries: u32,
    timeout_ms: u64,
) -> Result<FlashProtocol, String> {
    negotiate_flash_protocol(hello, artifact).map_err(|error| {
        if updater_cleanup_is_safe(hello) {
            cleanup_updater_session(device, retries, timeout_ms);
        }
        error
    })
}

fn read_updater_hello(
    device: &HidDevice,
    app: &AppHandle,
    total: u32,
    timeout_ms: u64,
    retries: u32,
) -> Result<UpdaterHello, String> {
    emit_flash_progress(app, "hello", 0, total);
    let hello = updater_transact(device, UPDATER_CMD_HELLO, 1, 0, &[], retries, timeout_ms)?;
    if hello.status != UPDATER_STATUS_OK {
        return Err(updater_status_error("HELLO", hello.status));
    }
    emit_flash_progress(app, "compatibility", 0, total);
    parse_updater_hello(&hello.payload)
}

fn read_updater_bootloader_info(
    device: &HidDevice,
    timeout_ms: u64,
    retries: u32,
) -> Result<Option<BootloaderInfo>, String> {
    let response = updater_transact(
        device,
        UPDATER_CMD_BOOTLOADER_INFO,
        0xB0,
        0,
        &[],
        retries,
        timeout_ms,
    )?;
    if response.status == UPDATER_STATUS_INVALID_COMMAND && response.payload.is_empty() {
        return Ok(None);
    }
    if response.status != UPDATER_STATUS_OK {
        return Err(updater_status_error("BOOTLOADER_INFO", response.status));
    }
    parse_bootloader_info(&response.payload).map(Some)
}

fn wait_for_updater_protocol(
    expected_serial_number: &str,
    expected_protocol: u16,
    wait_timeout_ms: u64,
    app: &AppHandle,
    total: u32,
    transaction_timeout_ms: u64,
    retries: u32,
) -> Result<(HidDevice, UpdaterHello), String> {
    let deadline = Instant::now() + Duration::from_millis(wait_timeout_ms);

    loop {
        let last_observation = match find_updater_device(expected_serial_number)? {
            Some(info) => match open_device_by_path(&info.path) {
                Ok(device) => {
                    verify_open_device_serial(&device, expected_serial_number)?;
                    match read_updater_hello(
                        &device,
                        app,
                        total,
                        transaction_timeout_ms.min(1_000),
                        retries.min(2),
                    ) {
                        Ok(hello) if hello.protocol_version == expected_protocol => {
                            return Ok((device, hello));
                        }
                        Ok(hello) => format!(
                            "observed protocol 0x{:04X}, waiting for 0x{expected_protocol:04X}",
                            hello.protocol_version
                        ),
                        Err(error) => format!("HELLO is not ready: {error}"),
                    }
                }
                Err(error) => format!("updater is enumerated but cannot be opened: {error}"),
            },
            None => "updater is currently disconnected".to_string(),
        };

        if Instant::now() >= deadline {
            return Err(format!(
                "timed out after {wait_timeout_ms} ms; last observation: {last_observation}"
            ));
        }
        std::thread::sleep(Duration::from_millis(DEVICE_POLL_MS));
    }
}

fn flash_after_hello(
    device: &HidDevice,
    hello: UpdaterHello,
    protocol: FlashProtocol,
    prepared: &PreparedFlashArtifact,
    app: &AppHandle,
    timeout_ms: u64,
    retries: u32,
) -> Result<FlashProtocol, String> {
    let mut seq = 1u8;
    let firmware_size = prepared.total;
    if firmware_size > hello.app_max_size {
        return Err(format!(
            "UPDATER_IMAGE_TOO_LARGE: selected artifact has {} bytes, updater protocol 0x{:04X} accepts at most {}",
            prepared.firmware.len(), hello.protocol_version, hello.app_max_size
        ));
    }

    if protocol.requires_auth() {
        // v3 authenticates the signed manifest on-device before BEGIN can erase.
        let mut authorization = firmware_manifest(
            &prepared.firmware,
            [
                prepared.version.major,
                prepared.version.minor,
                prepared.version.patch,
            ],
        )?;
        authorization.extend_from_slice(&prepared.signature);
        let mut auth_offset = 0usize;
        emit_flash_progress(app, "authenticating", 0, prepared.total);
        while auth_offset < authorization.len() {
            let end = (auth_offset + DATA_CHUNK_SIZE).min(authorization.len());
            seq = seq.wrapping_add(1);
            let auth = updater_transact(
                device,
                UPDATER_CMD_AUTH,
                seq,
                auth_offset as u32,
                &authorization[auth_offset..end],
                retries,
                timeout_ms,
            )?;
            if auth.status != UPDATER_STATUS_OK {
                return Err(format!(
                    "AUTH failed at manifest offset {auth_offset}: {} (0x{:02X})",
                    updater_status_name(auth.status),
                    auth.status,
                ));
            }
            auth_offset = end;
        }
    } else {
        // Protocol v2 has no AUTH command. Reaching this branch is only possible
        // for a structurally validated migration package whose signed inner v3
        // image was verified with the pinned release key before re-enumeration.
        emit_flash_progress(app, "legacyMigration", 0, prepared.total);
    }

    // BEGIN: metadata must match the pre-authorized signed manifest.
    seq = seq.wrapping_add(1);
    emit_flash_progress(app, "begin", 0, prepared.total);
    let mut begin_payload = [0u8; 12];
    begin_payload[0..4].copy_from_slice(&firmware_size.to_le_bytes());
    begin_payload[4..8].copy_from_slice(&prepared.image_crc32.to_le_bytes());
    begin_payload[8] = prepared.version.major;
    begin_payload[9] = prepared.version.minor;
    begin_payload[10] = prepared.version.patch;
    let begin = updater_transact(
        device,
        UPDATER_CMD_BEGIN,
        seq,
        0,
        &begin_payload,
        retries,
        timeout_ms.max(UPDATER_BEGIN_MIN_TIMEOUT_MS),
    )?;
    if begin.status != UPDATER_STATUS_OK {
        return Err(updater_status_error("BEGIN", begin.status));
    }

    // DATA chunks
    let mut offset = 0usize;
    let mut last_percent = u8::MAX;
    while offset < prepared.padded.len() {
        let end = (offset + DATA_CHUNK_SIZE).min(prepared.padded.len());
        let chunk = &prepared.padded[offset..end];
        seq = seq.wrapping_add(1);
        let resp = updater_transact(
            device,
            UPDATER_CMD_DATA,
            seq,
            offset as u32,
            chunk,
            retries,
            timeout_ms,
        )?;
        if resp.status != UPDATER_STATUS_OK {
            return Err(format!(
                "DATA failed at offset 0x{offset:08X}: {} (0x{:02X})",
                updater_status_name(resp.status),
                resp.status,
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

        let progress = (offset.min(prepared.firmware.len()) as u32).min(prepared.total);
        let percent = if prepared.total > 0 {
            ((progress as u64 * 100) / prepared.total as u64) as u8
        } else {
            100
        };
        if percent != last_percent {
            emit_flash_progress(app, "flashing", progress, prepared.total);
            last_percent = percent;
        }
    }

    // FINISH
    seq = seq.wrapping_add(1);
    emit_flash_progress(app, "finish", prepared.total, prepared.total);
    let finish = updater_transact(
        device,
        UPDATER_CMD_FINISH,
        seq,
        0,
        &[],
        retries,
        timeout_ms.max(5_000),
    )?;
    if finish.status != UPDATER_STATUS_OK {
        return Err(updater_status_error("FINISH", finish.status));
    }

    // BOOT (device may not respond — best effort)
    seq = seq.wrapping_add(1);
    emit_flash_progress(app, "boot", prepared.total, prepared.total);
    if let Ok(boot) = updater_transact(
        device,
        UPDATER_CMD_BOOT,
        seq,
        0,
        &[],
        retries.min(3),
        timeout_ms.min(2_000),
    ) {
        if boot.status != UPDATER_STATUS_OK {
            return Err(updater_status_error("BOOT", boot.status));
        }
    }

    Ok(protocol)
}

#[cfg(test)]
mod transport_tests {
    use super::{
        existing_application_recovery_command, firmware_flash_blocks_transport,
        is_supported_libhmk_rgb_effect, matches_libhmk_rgb_identity, normalize_rgb_bridge_response,
        parse_rgb_bridge_capabilities, read_sibling_firmware_signature,
        resolve_flash_target_snapshot, response_matches_request, rgb_bridge_report,
        rgb_bridge_response_matches_request, rgb_live_write_plan,
        run_bootloader_change_with_backup, select_unique_flash_device,
        select_unique_rgb_bridge_device, support_artifact_discovery_allowed,
        validate_app_only_recovery_protocol, validate_app_only_recovery_version,
        validate_firmware_carrier_name, DestructiveUpdaterOperation, FirmwareVersion,
        KbheDeviceKind, KbheHidDeviceInfo, KbheRgbBridgeDeviceInfo, UpdaterHello, KBHE_APP_PID,
        KBHE_LIBHMK_PID, KBHE_LIBHMK_USAGE, KBHE_LIBHMK_USAGE_PAGE, KBHE_PACKET_SIZE,
        KBHE_UPDATER_PID, KBHE_VID, RGB_CMD_FILL, RGB_CMD_GET_CAPABILITIES,
        RGB_CMD_SET_FRAME_CHUNK, UPDATER_CMD_BEGIN, UPDATER_CMD_BOOT, UPDATER_FLAG_APP_VALID,
        UPDATER_FLAG_SIGNATURE_REQUIRED, UPDATER_PROTOCOL_V2, UPDATER_PROTOCOL_V3,
    };

    fn firmware_device(
        kind: KbheDeviceKind,
        serial: Option<&str>,
        path: &str,
    ) -> KbheHidDeviceInfo {
        KbheHidDeviceInfo {
            path: path.to_string(),
            vid: KBHE_VID,
            pid: if kind == KbheDeviceKind::Runtime {
                KBHE_APP_PID
            } else {
                KBHE_UPDATER_PID
            },
            kind,
            interface_number: Some(1),
            usage_page: Some(0xFF00),
            usage: Some(1),
            manufacturer: Some("KBHE".to_string()),
            product: Some("KBHE".to_string()),
            serial_number: serial.map(str::to_string),
        }
    }

    fn rgb_bridge_device(serial: Option<&str>, path: &str) -> KbheRgbBridgeDeviceInfo {
        KbheRgbBridgeDeviceInfo {
            path: path.to_string(),
            vid: KBHE_VID,
            pid: KBHE_LIBHMK_PID,
            interface_number: Some(1),
            usage_page: Some(KBHE_LIBHMK_USAGE_PAGE),
            usage: Some(KBHE_LIBHMK_USAGE),
            manufacturer: Some("KBHE".to_string()),
            product: Some("KBHE libhmk".to_string()),
            serial_number: serial.map(str::to_string),
        }
    }

    #[test]
    fn indexed_runtime_response_must_echo_the_requested_key_and_profile() {
        let request = [0, 12, 2, 3];
        let matching = [0x40, 0, 12, 2, 3];
        let stale_key = [0x40, 0, 11, 2, 3];
        let stale_profile = [0x40, 0, 12, 1, 3];

        assert!(response_matches_request(0x40, &request, &matching));
        assert!(!response_matches_request(0x40, &request, &stale_key));
        assert!(!response_matches_request(0x40, &request, &stale_profile));
    }

    #[test]
    fn action_chunk_response_must_match_profile_program_and_offset() {
        let request = [4, 3, 7, 14, 4];
        let matching = [0x92, 0, 3, 7, 14, 4];
        let stale = [0x92, 0, 3, 7, 0, 4];

        assert!(response_matches_request(0x92, &request, &matching));
        assert!(!response_matches_request(0x92, &request, &stale));
    }

    #[test]
    fn action_state_response_must_match_index_and_value() {
        let request = [2, 6, 1];
        let matching = [0x9D, 0, 6, 1];
        let stale_index = [0x9D, 0, 5, 1];
        let stale_value = [0x9D, 0, 6, 0];

        assert!(response_matches_request(0x9D, &request, &matching));
        assert!(!response_matches_request(0x9D, &request, &stale_index));
        assert!(!response_matches_request(0x9D, &request, &stale_value));
    }

    #[test]
    fn libhmk_discovery_requires_pid_and_raw_hid_usage() {
        assert!(matches_libhmk_rgb_identity(
            KBHE_VID,
            KBHE_LIBHMK_PID,
            KBHE_LIBHMK_USAGE_PAGE,
            KBHE_LIBHMK_USAGE,
        ));
        assert!(!matches_libhmk_rgb_identity(
            KBHE_VID,
            KBHE_APP_PID,
            KBHE_LIBHMK_USAGE_PAGE,
            KBHE_LIBHMK_USAGE,
        ));
        assert!(!matches_libhmk_rgb_identity(
            KBHE_VID,
            KBHE_LIBHMK_PID,
            0xFF00,
            1,
        ));
    }

    #[test]
    fn libhmk_rgb_target_requires_matching_path_and_serial() {
        let target = rgb_bridge_device(Some("TARGET"), "target-path");
        let other = rgb_bridge_device(Some("OTHER"), "other-path");
        let selected = select_unique_rgb_bridge_device(&[other, target], "target-path", " TARGET ")
            .expect("matching RGB target");
        assert_eq!(selected.path, "target-path");
        assert_eq!(selected.serial_number.as_deref(), Some("TARGET"));

        let target = rgb_bridge_device(Some("TARGET"), "target-path");
        assert!(
            select_unique_rgb_bridge_device(&[target], "stale-path", "TARGET")
                .unwrap_err()
                .contains("path no longer belongs")
        );
    }

    #[test]
    fn libhmk_rgb_target_refuses_missing_or_duplicate_serials() {
        let target = rgb_bridge_device(Some("TARGET"), "target-path");
        assert!(
            select_unique_rgb_bridge_device(&[target.clone()], "target-path", " ")
                .unwrap_err()
                .contains("non-empty USB serial")
        );

        let duplicate = rgb_bridge_device(Some("TARGET"), "duplicate-path");
        assert!(
            select_unique_rgb_bridge_device(&[target, duplicate], "target-path", "TARGET",)
                .unwrap_err()
                .contains("ambiguous")
        );
    }

    #[test]
    fn rgb_bridge_report_reserves_status_byte_and_rejects_oversize() {
        let report = rgb_bridge_report(0x6C, &[1, 2, 3]).expect("valid fill report");
        assert_eq!(report[0], 0);
        assert_eq!(report[1], 0x6C);
        assert_eq!(report[2], 0);
        assert_eq!(&report[3..6], &[1, 2, 3]);
        assert!(rgb_bridge_report(0x6C, &[0; KBHE_PACKET_SIZE - 1]).is_err());
    }

    #[test]
    fn rgb_bridge_response_accepts_optional_report_id_only() {
        let raw = [0x7Fu8; KBHE_PACKET_SIZE];
        assert_eq!(normalize_rgb_bridge_response(&raw).unwrap(), raw);

        let mut with_id = [0u8; KBHE_PACKET_SIZE + 1];
        with_id[1..].copy_from_slice(&raw);
        assert_eq!(normalize_rgb_bridge_response(&with_id).unwrap(), raw);

        with_id[0] = 1;
        assert!(normalize_rgb_bridge_response(&with_id).is_err());
    }

    #[test]
    fn rgb_bridge_writes_ignore_stale_same_command_acknowledgements() {
        let fill = [RGB_CMD_FILL, 0, 10, 20, 30];
        assert!(rgb_bridge_response_matches_request(
            RGB_CMD_FILL,
            &[10, 20, 30],
            &fill,
        ));
        assert!(!rgb_bridge_response_matches_request(
            RGB_CMD_FILL,
            &[10, 21, 30],
            &fill,
        ));

        let chunk = [RGB_CMD_SET_FRAME_CHUNK, 0, 3, 60];
        assert!(rgb_bridge_response_matches_request(
            RGB_CMD_SET_FRAME_CHUNK,
            &[3, 60, 1, 2, 3],
            &chunk,
        ));
        assert!(!rgb_bridge_response_matches_request(
            RGB_CMD_SET_FRAME_CHUNK,
            &[4, 60, 1, 2, 3],
            &chunk,
        ));
    }

    #[test]
    fn rgb_bridge_capabilities_are_strictly_validated() {
        let mut response = [0u8; KBHE_PACKET_SIZE];
        response[0] = RGB_CMD_GET_CAPABILITIES;
        response[2] = 1;
        response[4] = 82;
        response[5] = 3;
        response[6] = 60;
        response[7] = 7;
        response[8] = 0x7F;

        let parsed = parse_rgb_bridge_capabilities(&response).expect("valid capabilities");
        assert_eq!(parsed.led_count, 82);
        assert_eq!(parsed.chunk_bytes, 60);

        response[2] = 2;
        assert!(parse_rgb_bridge_capabilities(&response).is_err());
        response[2] = 1;
        response[5] = 4;
        assert!(parse_rgb_bridge_capabilities(&response).is_err());
        response[5] = 3;
        response[10] = 1;
        assert!(parse_rgb_bridge_capabilities(&response).is_err());
    }

    #[test]
    fn libhmk_effect_allowlist_cannot_reach_native_effect_ids() {
        for effect in [0, 1, 2, 3, 7] {
            assert!(is_supported_libhmk_rgb_effect(effect));
        }
        for effect in [4, 6, 8, 42, 255] {
            assert!(!is_supported_libhmk_rgb_effect(effect));
        }
    }

    #[test]
    fn pixel_and_frame_writes_enter_live_and_rollback_only_when_needed() {
        let animated = rgb_live_write_plan(2, 7);
        assert!(animated.enter_live);
        assert!(animated.restore_effect_on_failure);

        let already_live = rgb_live_write_plan(7, 7);
        assert!(!already_live.enter_live);
        assert!(!already_live.restore_effect_on_failure);
    }

    #[test]
    fn native_transport_is_blocked_for_the_entire_flash_lease() {
        assert!(firmware_flash_blocks_transport(true));
        assert!(!firmware_flash_blocks_transport(false));
    }

    #[test]
    fn native_signature_reader_accepts_only_the_exact_bounded_sibling() {
        use std::time::{SystemTime, UNIX_EPOCH};

        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!(
            "kbhe-signature-reader-{}-{nonce}",
            std::process::id()
        ));
        std::fs::create_dir(&directory).unwrap();
        let firmware = directory.join("kbhe-app.bin");
        let signature = directory.join("kbhe-app.bin.sig");
        std::fs::write(&firmware, [0xA5; 8]).unwrap();
        std::fs::write(&signature, [0x5A; 64]).unwrap();

        let bytes = read_sibling_firmware_signature(firmware.to_str().unwrap()).unwrap();
        assert_eq!(bytes, vec![0x5A; 64]);

        std::fs::write(&signature, [0x5A; 63]).unwrap();
        assert!(read_sibling_firmware_signature(firmware.to_str().unwrap())
            .unwrap_err()
            .contains("expected 64"));
        assert!(read_sibling_firmware_signature("relative.bin")
            .unwrap_err()
            .contains("absolute .bin"));

        std::fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn schema_one_and_two_cannot_reach_bootloader_begin() {
        use std::cell::Cell;

        for legacy_schema in [1, 2] {
            let begin_calls = Cell::new(0u8);
            let error = run_bootloader_change_with_backup(
                DestructiveUpdaterOperation::V3Refresh,
                false,
                || {
                    begin_calls.set(begin_calls.get() + 1);
                    Ok(())
                },
            )
            .unwrap_err();
            assert_eq!(begin_calls.get(), 0, "schema {legacy_schema} reached BEGIN");
            assert!(error.contains("refused before BEGIN"));
        }

        let begin_calls = Cell::new(0u8);
        run_bootloader_change_with_backup(DestructiveUpdaterOperation::V3Refresh, true, || {
            begin_calls.set(begin_calls.get() + 1);
            Ok(())
        })
        .unwrap();
        assert_eq!(begin_calls.get(), 1);
    }

    #[test]
    fn bootloader_migration_uses_the_same_pre_begin_backup_gate() {
        let error = run_bootloader_change_with_backup(
            DestructiveUpdaterOperation::V2ToV3Migration,
            false,
            || -> Result<(), String> {
                panic!("migration BEGIN must not be reachable without schema 3")
            },
        )
        .unwrap_err();
        assert!(error.contains("v2-to-v3 migration"));
        assert!(error.contains("refused before BEGIN"));
    }

    #[test]
    fn local_firmware_selection_uses_the_exact_versioned_carrier_role() {
        let legacy = FirmwareVersion {
            major: 2,
            minor: 0,
            patch: 9,
        };
        let refresh = FirmwareVersion {
            major: 2,
            minor: 0,
            patch: 10,
        };
        assert!(validate_firmware_carrier_name("/tmp/kbhe-app.bin", legacy).is_ok());
        assert!(validate_firmware_carrier_name("/tmp/kbhe-app-updater-v3.bin", refresh,).is_ok());
        assert!(validate_firmware_carrier_name("/tmp/kbhe-app.bin", refresh)
            .unwrap_err()
            .contains("UPDATER_CARRIER_ROLE_MISMATCH"));
        assert!(validate_firmware_carrier_name("/tmp/kbhe-app-updater-v3.bin", legacy,).is_err());
    }

    #[test]
    fn explicit_app_only_recovery_disables_support_discovery_and_requires_v3() {
        assert!(support_artifact_discovery_allowed(false));
        assert!(!support_artifact_discovery_allowed(true));
        assert!(validate_app_only_recovery_protocol(true, UPDATER_PROTOCOL_V3).is_ok());
        let error = validate_app_only_recovery_protocol(true, UPDATER_PROTOCOL_V2).unwrap_err();
        assert!(error.contains("requires updater v3"));
        assert!(error.contains("No BEGIN was sent"));

        let hello = UpdaterHello {
            protocol_version: UPDATER_PROTOCOL_V3,
            flags: 0,
            app_base: 0x0801_0000,
            app_max_size: 0x0002_FF00,
            write_align: 4,
            installed_version: [2, 0, 9],
        };
        assert!(validate_app_only_recovery_version(
            hello,
            FirmwareVersion {
                major: 2,
                minor: 0,
                patch: 9,
            },
        )
        .is_ok());
        let mismatch = validate_app_only_recovery_version(
            hello,
            FirmwareVersion {
                major: 2,
                minor: 0,
                patch: 10,
            },
        )
        .unwrap_err();
        assert!(mismatch.contains("exact installed application version"));
        assert!(mismatch.contains("No BEGIN was sent"));
    }

    #[test]
    fn existing_v3_application_recovery_plans_boot_only() {
        let valid = UpdaterHello {
            protocol_version: UPDATER_PROTOCOL_V3,
            flags: UPDATER_FLAG_SIGNATURE_REQUIRED | UPDATER_FLAG_APP_VALID,
            app_base: 0x0801_0000,
            app_max_size: 0x0002_FF00,
            write_align: 4,
            installed_version: [2, 0, 9],
        };
        let command = existing_application_recovery_command(valid).unwrap();
        assert_eq!(command, UPDATER_CMD_BOOT);
        assert_ne!(command, UPDATER_CMD_BEGIN);

        let invalid = UpdaterHello {
            flags: UPDATER_FLAG_SIGNATURE_REQUIRED,
            ..valid
        };
        assert!(existing_application_recovery_command(invalid)
            .unwrap_err()
            .contains("no destructive command"));

        let v2 = UpdaterHello {
            protocol_version: UPDATER_PROTOCOL_V2,
            flags: UPDATER_FLAG_APP_VALID,
            app_max_size: 0x0004_FF00,
            ..valid
        };
        assert!(existing_application_recovery_command(v2).is_err());
    }

    #[test]
    fn firmware_target_tracks_one_serial_across_reenumeration() {
        let other = firmware_device(KbheDeviceKind::Runtime, Some("OTHER"), "other");
        let runtime = firmware_device(KbheDeviceKind::Runtime, Some("TARGET"), "runtime");
        let (selected_runtime, selected_updater) =
            resolve_flash_target_snapshot(&[other.clone(), runtime], "TARGET").unwrap();
        assert_eq!(selected_runtime.unwrap().path, "runtime");
        assert!(selected_updater.is_none());

        let updater = firmware_device(KbheDeviceKind::Updater, Some("TARGET"), "updater");
        let (selected_runtime, selected_updater) =
            resolve_flash_target_snapshot(&[other, updater], "TARGET").unwrap();
        assert!(selected_runtime.is_none());
        assert_eq!(selected_updater.unwrap().path, "updater");
    }

    #[test]
    fn firmware_target_refuses_duplicate_or_cross_mode_serials() {
        let duplicate = [
            firmware_device(KbheDeviceKind::Updater, Some("DUP"), "updater-a"),
            firmware_device(KbheDeviceKind::Updater, Some("DUP"), "updater-b"),
        ];
        assert!(
            select_unique_flash_device(&duplicate, KbheDeviceKind::Updater, "DUP")
                .unwrap_err()
                .contains("ambiguous")
        );

        let cross_mode = [
            firmware_device(KbheDeviceKind::Runtime, Some("BOTH"), "runtime"),
            firmware_device(KbheDeviceKind::Updater, Some("BOTH"), "updater"),
        ];
        assert!(resolve_flash_target_snapshot(&cross_mode, "BOTH")
            .unwrap_err()
            .contains("both runtime and updater"));
    }
}
