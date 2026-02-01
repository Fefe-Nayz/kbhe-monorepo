// https://crates.io/crates/hidapi/2.6.4

//HidDevice is used to initiate conection to a device when you know it's vendor and product ID 
//let mut device: HidDevice = api.open();
use hidapi::{ HidApi, DeviceInfo, HidDevice };

extern crate hidapi;

#[tauri::command]
pub fn list_hid_devices() {
    let api = HidApi::new().unwrap();

    //Create a new list to handle multiple devices showing at the same time 
    let mut my_device_list: Vec<DeviceInfo> = Vec::new();

    for device in api.device_list() {
        let device_in_list = my_device_list.iter().any(
            |d| d.vendor_id() == device.vendor_id() && d.product_id() == d.product_id());
        if !device_in_list{
            my_device_list.push(device.clone());
        }
    }

    // Print out information about all connected devices
    for device in &my_device_list {
        println!("VID : {:04x}, PID {:04x}, Product : {}, Manufacturer : {}", 
        device.vendor_id(),
        device.product_id(),
        device.product_string().unwrap_or("Unknown".into()),
        device.manufacturer_string().unwrap_or("Unknown".into()));
    }

    /*
    Did a few test with my laptop onboard Pointing Device but need to run as Administrator

    let device = api.open(0xVID, 0xPID).unwrap();
    let mut buf = [0u8, 64];
    match device.read(&mut buf){
        Ok(n) =>{
            println!("Data {} from pointing device {:02x?} ", n, &buf[..n]);
        }
        Err(e)=>{
            println!("We have an error with : {}", e);
        }
    }
     */

    println!("Number of connected HID devices: {}", my_device_list.len());
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