// WASAPI loopback audio capture + FFT spectrum analysis.
// Mirrors the Python windows_volume.py implementation using AGC, spectral
// tilt, and independent rise/fall smoothing per band.

use rustfft::{num_complex::Complex, FftPlanner};
use std::sync::{Mutex, OnceLock};
use std::time::Instant;

const BAND_COUNT: usize = 16;

struct AudioState {
    agc_level: f32,
    smooth_levels: [f32; BAND_COUNT],
    last_tick: Option<Instant>,
}

impl Default for AudioState {
    fn default() -> Self {
        Self {
            agc_level: 1e-6,
            smooth_levels: [0.0; BAND_COUNT],
            last_tick: None,
        }
    }
}

static AUDIO_STATE: OnceLock<Mutex<AudioState>> = OnceLock::new();

fn audio_state() -> &'static Mutex<AudioState> {
    AUDIO_STATE.get_or_init(Default::default)
}

// ---------------------------------------------------------------------------
// Geometry helpers (equivalent to np.geomspace)
// ---------------------------------------------------------------------------

fn geomspace(start: f32, stop: f32, n: usize) -> Vec<f32> {
    if n == 1 {
        return vec![start];
    }
    let ln_start = start.ln();
    let ln_stop = stop.ln();
    (0..n)
        .map(|i| {
            let t = i as f32 / (n - 1) as f32;
            (ln_start + (ln_stop - ln_start) * t).exp()
        })
        .collect()
}

// ---------------------------------------------------------------------------
// Raw spectrum bands from mono PCM samples (no smoothing)
// ---------------------------------------------------------------------------

fn compute_raw_bands(samples: &[f32], sample_rate: u32) -> [f32; BAND_COUNT] {
    let n = samples.len();
    if n < 64 {
        return [0.0; BAND_COUNT];
    }

    // DC-remove
    let mean = samples.iter().sum::<f32>() / n as f32;

    // Hanning window + prepare complex FFT input (zero-pad to next power of 2)
    let fft_size = n.next_power_of_two();
    let mut input: Vec<Complex<f32>> = (0..fft_size)
        .map(|i| {
            if i < n {
                let w =
                    0.5 * (1.0 - (2.0 * std::f32::consts::PI * i as f32 / (n - 1) as f32).cos());
                Complex::new((samples[i] - mean) * w, 0.0)
            } else {
                Complex::new(0.0, 0.0)
            }
        })
        .collect();

    let mut planner = FftPlanner::<f32>::new();
    planner.plan_fft_forward(fft_size).process(&mut input);

    // Positive spectrum: bins 0..fft_size/2 (equivalent to rfft output)
    let half = fft_size / 2;
    let magnitudes: Vec<f32> = input[..=half]
        .iter()
        .map(|c| (c.re * c.re + c.im * c.im).sqrt())
        .collect();

    // Frequency resolution
    let bin_hz = sample_rate as f32 / fft_size as f32;

    // Geomspace band edges: 35 Hz → 12000 Hz (matches Python)
    let edges = geomspace(35.0, 12_000.0, BAND_COUNT + 1);

    let mut bands = [0.0f32; BAND_COUNT];
    for b in 0..BAND_COUNT {
        let lo = edges[b];
        let hi = edges[b + 1];

        let bin_lo = (lo / bin_hz).floor() as usize;
        let bin_hi = (hi / bin_hz).ceil() as usize;
        let bin_lo = bin_lo.max(1);
        let bin_hi = bin_hi.min(magnitudes.len().saturating_sub(1));

        if bin_lo > bin_hi {
            continue;
        }

        let slice = &magnitudes[bin_lo..=bin_hi];
        // RMS energy per band
        let energy = (slice.iter().map(|&v| v * v).sum::<f32>() / slice.len() as f32).sqrt();

        // Spectral tilt: boost higher bands (mirrors Python tilt formula)
        let tilt = 1.0 + 1.6 * ((b as f32 / (BAND_COUNT - 1) as f32).powf(1.15));

        bands[b] = energy * tilt;
    }

    bands
}

// ---------------------------------------------------------------------------
// Smooth + AGC application (matches Python rise/fall + AGC logic)
// ---------------------------------------------------------------------------

fn apply_agc_and_smooth(raw_bands: &[f32; BAND_COUNT]) -> [u8; BAND_COUNT] {
    let mut state = audio_state().lock().unwrap_or_else(|p| p.into_inner());

    let now = Instant::now();
    let dt = match state.last_tick {
        Some(prev) => {
            let elapsed = now.duration_since(prev).as_secs_f32();
            elapsed.clamp(0.005, 0.250)
        }
        None => 1.0 / 30.0,
    };
    state.last_tick = Some(now);

    // AGC: fast attack, slow decay (mirrors Python coefficients)
    let frame_peak = raw_bands.iter().cloned().fold(0.0f32, f32::max);
    if frame_peak >= state.agc_level {
        state.agc_level += (frame_peak - state.agc_level) * (dt * 9.0).min(1.0);
    } else {
        state.agc_level += (frame_peak - state.agc_level) * (dt * 1.6).min(1.0);
    }
    state.agc_level = state.agc_level.max(1e-6);

    let scale = state.agc_level * 1.12;

    // Per-band smoothing: fast rise, slow fall
    let rise_rate = 16.0f32;
    let fall_per_second = 2.8f32;

    let mut output = [0u8; BAND_COUNT];
    for b in 0..BAND_COUNT {
        let mut target = (raw_bands[b] / scale).clamp(0.0, 1.0);
        // Gamma (matches Python target ** 0.78)
        target = target.powf(0.78);

        let prev = state.smooth_levels[b];
        let next = if target >= prev {
            prev + (target - prev) * (dt * rise_rate).min(1.0)
        } else {
            (prev - fall_per_second * dt).max(target)
        };
        state.smooth_levels[b] = next;

        // Output gamma (matches Python nxt ** 0.85)
        output[b] = (next.powf(0.85) * 255.0).clamp(0.0, 255.0) as u8;
    }

    output
}

// ---------------------------------------------------------------------------
// Platform-specific loopback capture
// ---------------------------------------------------------------------------

#[cfg(target_os = "windows")]
mod platform {
    use super::*;
    use windows::Win32::Media::Audio::{
        eConsole, eRender, IAudioCaptureClient, IAudioClient, IMMDeviceEnumerator,
        MMDeviceEnumerator, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
    };
    use windows::Win32::System::Com::{
        CoCreateInstance, CoInitializeEx, CoTaskMemFree, CoUninitialize, CLSCTX_ALL,
        COINIT_MULTITHREADED,
    };

    // WAVE_FORMAT_IEEE_FLOAT = 3, WAVE_FORMAT_EXTENSIBLE = 0xFFFE
    const WAVE_FORMAT_IEEE_FLOAT: u16 = 3;
    const WAVE_FORMAT_EXTENSIBLE: u16 = 0xFFFE;

    unsafe fn loopback_capture(
        sample_rate_out: &mut u32,
    ) -> windows::core::Result<Vec<f32>> {
        let enumerator: IMMDeviceEnumerator =
            CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)?;
        let device = enumerator.GetDefaultAudioEndpoint(eRender, eConsole)?;
        let audio_client: IAudioClient = device.Activate(CLSCTX_ALL, None)?;

        let fmt_ptr = audio_client.GetMixFormat()?;
        let fmt = &*fmt_ptr;
        let sample_rate = fmt.nSamplesPerSec;
        let channels = fmt.nChannels as usize;
        let is_float =
            fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT || fmt.wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        *sample_rate_out = sample_rate;

        // 1-second WASAPI buffer (units: 100-nanosecond intervals)
        let buffer_duration: i64 = 10_000_000;
        let init_result = audio_client.Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            buffer_duration,
            0,
            fmt_ptr,
            None,
        );
        CoTaskMemFree(Some(fmt_ptr.cast()));
        init_result?;

        audio_client.Start()?;

        // Accumulate ~1024 frames of audio (matches Python numframes=1024 at 48 kHz ≈ 21 ms)
        std::thread::sleep(std::time::Duration::from_millis(25));

        let capture_client: IAudioCaptureClient = audio_client.GetService()?;
        let mut mono: Vec<f32> = Vec::with_capacity(2048);

        loop {
            let next_packet = match capture_client.GetNextPacketSize() {
                Ok(n) => n,
                Err(_) => break,
            };
            if next_packet == 0 {
                break;
            }

            let mut data_ptr = std::ptr::null_mut::<u8>();
            let mut frames = 0u32;
            let mut flags = 0u32;

            capture_client.GetBuffer(&mut data_ptr, &mut frames, &mut flags, None, None)?;

            if frames > 0 && !data_ptr.is_null() && is_float {
                let floats = std::slice::from_raw_parts(
                    data_ptr as *const f32,
                    frames as usize * channels,
                );
                for frame in floats.chunks_exact(channels) {
                    let m: f32 = frame.iter().sum::<f32>() / channels as f32;
                    mono.push(m);
                }
            }

            capture_client.ReleaseBuffer(frames)?;
        }

        audio_client.Stop()?;
        Ok(mono)
    }

    pub fn get_audio_bands_impl() -> Result<Vec<u8>, String> {
        let mut sample_rate = 48_000u32;
        let samples = unsafe {
            let init_ok = CoInitializeEx(None, COINIT_MULTITHREADED).is_ok();
            let result = loopback_capture(&mut sample_rate).map_err(|e| e.to_string());
            if init_ok {
                CoUninitialize();
            }
            result?
        };

        let raw = compute_raw_bands(&samples, sample_rate);
        Ok(apply_agc_and_smooth(&raw).to_vec())
    }
}

#[cfg(not(target_os = "windows"))]
mod platform {
    use super::*;

    pub fn get_audio_bands_impl() -> Result<Vec<u8>, String> {
        // Non-Windows: return silence; AGC still ticks correctly.
        let raw = [0.0f32; BAND_COUNT];
        Ok(apply_agc_and_smooth(&raw).to_vec())
    }
}

#[tauri::command]
pub fn kbhe_get_audio_bands() -> Result<Vec<u8>, String> {
    platform::get_audio_bands_impl()
}
