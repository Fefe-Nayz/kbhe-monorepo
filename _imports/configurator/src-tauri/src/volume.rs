#[cfg(target_os = "windows")]
mod platform {
    use windows::Win32::Media::Audio::{
        eConsole, eRender, IMMDeviceEnumerator, MMDeviceEnumerator,
    };
    use windows::Win32::Media::Audio::Endpoints::IAudioEndpointVolume;
    use windows::Win32::System::Com::{
        CoCreateInstance, CoInitializeEx, CoUninitialize, CLSCTX_ALL, COINIT_MULTITHREADED,
    };

    pub fn get_system_volume() -> Option<u8> {
        unsafe {
            let init_ok = CoInitializeEx(None, COINIT_MULTITHREADED).is_ok();

            let result = (|| -> windows::core::Result<u8> {
                let enumerator: IMMDeviceEnumerator =
                    CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)?;
                let device = enumerator.GetDefaultAudioEndpoint(eRender, eConsole)?;
                let volume: IAudioEndpointVolume = device.Activate(CLSCTX_ALL, None)?;
                let scalar = volume.GetMasterVolumeLevelScalar()?;
                let level = (scalar.clamp(0.0, 1.0) * 255.0).round() as u8;
                Ok(level)
            })();

            if init_ok {
                CoUninitialize();
            }

            result.ok()
        }
    }
}

#[cfg(not(target_os = "windows"))]
mod platform {
    pub fn get_system_volume() -> Option<u8> {
        None
    }
}

pub use platform::get_system_volume;

#[tauri::command]
pub fn kbhe_get_system_volume() -> Option<u8> {
    get_system_volume()
}
