// https://crates.io/crates/hidapi/2.6.4

extern crate hidapi;

#[tauri::command]
pub fn list_hid_devices() {
    let api = hidapi::HidApi::new().unwrap();
    // Print out information about all connected devices
    for device in api.device_list() {
        println!("The device {:#?}", device);
    }
    println!("Number of connected HID devices: {}", api.device_list().count());
}

/* 
#[tauri::command]
pub fn connect() {
    let api = hidapi::HidApi::new().unwrap();
    let (VID, PID) = (0x0123, 0x3456);
    let device = api.open(VID, PID).unwrap();
}

#[tauri::command]
pub fn read() {
    let mut buf = [0u8; 8];
    let res = device.read(&mut buf[..]).unwrap();
    println!("Read: {:?}", &buf[..res]);
}

#[tauri::command]
pub fn write() {
    println!("Writing to device...");
}

*/