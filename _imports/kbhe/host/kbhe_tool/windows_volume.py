from __future__ import annotations

import ctypes
import time
import warnings
from ctypes import wintypes
from typing import Optional

try:
    import numpy as _np
except Exception:
    _np = None

try:
    import soundcard as _soundcard
except Exception:
    _soundcard = None


if not hasattr(ctypes, "WINFUNCTYPE"):
    WINFUNCTYPE = ctypes.CFUNCTYPE
else:
    WINFUNCTYPE = ctypes.WINFUNCTYPE

HRESULT = ctypes.c_long
CLSCTX_ALL = 23
COINIT_MULTITHREADED = 0x0
RPC_E_CHANGED_MODE = 0x80010106
E_RENDER = 0
E_CONSOLE = 0


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


def _guid(value: str) -> GUID:
    text = value.strip("{}")
    left, middle1, middle2, right1, right2 = text.split("-")
    data4 = bytes.fromhex(right1 + right2)
    return GUID(
        int(left, 16),
        int(middle1, 16),
        int(middle2, 16),
        (ctypes.c_ubyte * 8).from_buffer_copy(data4),
    )


CLSID_MMDeviceEnumerator = _guid("{BCDE0395-E52F-467C-8E3D-C4579291692E}")
IID_IMMDeviceEnumerator = _guid("{A95664D2-9614-4F35-A746-DE8DB63617E6}")
IID_IAudioEndpointVolume = _guid("{5CDF2C82-841E-4546-9722-0CF74078229A}")
IID_IAudioMeterInformation = _guid("{C02216F6-8C67-4B5B-9D00-D008E73E0064}")

_last_spectrum_levels: list[int] = []
_last_spectrum_time: float = 0.0
_last_spectrum_source: str = "none"
_loopback_recorder = None
_loopback_recorder_key: str | None = None
_loopback_smooth_levels: list[float] = []
_loopback_agc_level: float = 1.0
_loopback_last_time: float = 0.0

ole32 = ctypes.WinDLL("ole32")
ole32.CoInitializeEx.argtypes = [ctypes.c_void_p, wintypes.DWORD]
ole32.CoInitializeEx.restype = HRESULT
ole32.CoUninitialize.argtypes = []
ole32.CoUninitialize.restype = None
ole32.CoCreateInstance.argtypes = [
    ctypes.POINTER(GUID),
    ctypes.c_void_p,
    wintypes.DWORD,
    ctypes.POINTER(GUID),
    ctypes.POINTER(ctypes.c_void_p),
]
ole32.CoCreateInstance.restype = HRESULT


def _succeeded(hr: int) -> bool:
    return int(hr) >= 0


def _coinit() -> bool:
    hr = ole32.CoInitializeEx(None, COINIT_MULTITHREADED)
    if hr in (0, 1):  # S_OK, S_FALSE
        return True
    if ctypes.c_ulong(hr).value == RPC_E_CHANGED_MODE:
        return False
    raise OSError(f"CoInitializeEx failed: 0x{ctypes.c_ulong(hr).value:08X}")


def _release(ptr: ctypes.c_void_p) -> None:
    if not ptr:
        return
    vtbl = ctypes.cast(ptr, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    release_addr = ctypes.cast(vtbl[2], ctypes.c_void_p).value
    if not release_addr:
        return
    release_fn = WINFUNCTYPE(wintypes.ULONG, ctypes.c_void_p)(release_addr)
    release_fn(ptr)


def _get_default_audio_endpoint_interface(iid: GUID) -> ctypes.c_void_p:
    enumerator = ctypes.c_void_p()
    device = ctypes.c_void_p()
    endpoint = ctypes.c_void_p()
    try:
        hr = ole32.CoCreateInstance(
            ctypes.byref(CLSID_MMDeviceEnumerator),
            None,
            CLSCTX_ALL,
            ctypes.byref(IID_IMMDeviceEnumerator),
            ctypes.byref(enumerator),
        )
        if not _succeeded(hr):
            raise OSError(f"CoCreateInstance failed: 0x{ctypes.c_ulong(hr).value:08X}")

        enum_vtbl = ctypes.cast(enumerator, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        get_default_audio_endpoint_addr = ctypes.cast(enum_vtbl[4], ctypes.c_void_p).value
        if get_default_audio_endpoint_addr is None:
            raise OSError("IMMDeviceEnumerator vtbl[4] is null")
        get_default_audio_endpoint = WINFUNCTYPE(
            HRESULT, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)
        )(int(get_default_audio_endpoint_addr))
        hr = get_default_audio_endpoint(enumerator, E_RENDER, E_CONSOLE, ctypes.byref(device))
        if not _succeeded(hr):
            raise OSError(f"GetDefaultAudioEndpoint failed: 0x{ctypes.c_ulong(hr).value:08X}")

        device_vtbl = ctypes.cast(device, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        activate_addr = ctypes.cast(device_vtbl[3], ctypes.c_void_p).value
        if activate_addr is None:
            raise OSError("IMMDevice vtbl[3] is null")
        activate = WINFUNCTYPE(
            HRESULT,
            ctypes.c_void_p,
            ctypes.POINTER(GUID),
            wintypes.DWORD,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p),
        )(int(activate_addr))
        hr = activate(
            device,
            ctypes.byref(iid),
            CLSCTX_ALL,
            None,
            ctypes.byref(endpoint),
        )
        if not _succeeded(hr):
            raise OSError(f"IMMDevice::Activate failed: 0x{ctypes.c_ulong(hr).value:08X}")

        return endpoint
    finally:
        _release(device)
        _release(enumerator)


def _get_default_audio_endpoint_volume() -> ctypes.c_void_p:
    return _get_default_audio_endpoint_interface(IID_IAudioEndpointVolume)


def _get_default_audio_endpoint_meter() -> ctypes.c_void_p:
    return _get_default_audio_endpoint_interface(IID_IAudioMeterInformation)


def get_default_render_volume_scalar() -> Optional[float]:
    endpoint = ctypes.c_void_p()
    initialized_here = False
    try:
        initialized_here = _coinit()
        endpoint = _get_default_audio_endpoint_volume()
        endpoint_vtbl = ctypes.cast(endpoint, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        get_scalar_addr = ctypes.cast(endpoint_vtbl[9], ctypes.c_void_p).value
        if get_scalar_addr is None:
            raise OSError("IAudioEndpointVolume vtbl[9] is null")
        get_scalar = WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(ctypes.c_float))(
            int(get_scalar_addr)
        )
        level = ctypes.c_float()
        hr = get_scalar(endpoint, ctypes.byref(level))
        if not _succeeded(hr):
            raise OSError(f"GetMasterVolumeLevelScalar failed: 0x{ctypes.c_ulong(hr).value:08X}")
        value = max(0.0, min(1.0, float(level.value)))
        return value
    except Exception:
        return None
    finally:
        _release(endpoint)
        if initialized_here:
            ole32.CoUninitialize()


def get_default_render_volume_level() -> Optional[int]:
    scalar = get_default_render_volume_scalar()
    if scalar is None:
        return None
    return max(0, min(255, int(round(scalar * 255.0))))


def get_default_render_peak_scalar() -> Optional[float]:
    endpoint = ctypes.c_void_p()
    initialized_here = False
    try:
        initialized_here = _coinit()
        endpoint = _get_default_audio_endpoint_meter()
        endpoint_vtbl = ctypes.cast(
            endpoint, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))
        ).contents
        get_peak_addr = ctypes.cast(endpoint_vtbl[3], ctypes.c_void_p).value
        if get_peak_addr is None:
            raise OSError("IAudioMeterInformation vtbl[3] is null")
        get_peak = WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(ctypes.c_float))(
            int(get_peak_addr)
        )
        peak = ctypes.c_float()
        hr = get_peak(endpoint, ctypes.byref(peak))
        if not _succeeded(hr):
            raise OSError(
                f"IAudioMeterInformation::GetPeakValue failed: 0x{ctypes.c_ulong(hr).value:08X}"
            )
        return max(0.0, min(1.0, float(peak.value)))
    except Exception:
        return None
    finally:
        _release(endpoint)
        if initialized_here:
            ole32.CoUninitialize()


def _close_loopback_recorder() -> None:
    global _loopback_recorder, _loopback_recorder_key

    recorder = _loopback_recorder
    _loopback_recorder = None
    _loopback_recorder_key = None

    if recorder is None:
        return

    try:
        recorder.__exit__(None, None, None)
    except Exception:
        pass


def _get_loopback_recorder():
    global _loopback_recorder, _loopback_recorder_key

    if _soundcard is None:
        return None

    try:
        speaker = _soundcard.default_speaker()
        if speaker is None:
            return None
        speaker_name = str(getattr(speaker, "name", "")).strip()
        if not speaker_name:
            return None

        loopback_mic = _soundcard.get_microphone(speaker_name, include_loopback=True)
        if loopback_mic is None:
            return None

        key = f"loopback:{speaker_name}"
        if _loopback_recorder is not None and _loopback_recorder_key == key:
            return _loopback_recorder

        _close_loopback_recorder()
        recorder = loopback_mic.recorder(samplerate=48000, channels=2, blocksize=1024)
        recorder.__enter__()
        _loopback_recorder = recorder
        _loopback_recorder_key = key
        return recorder
    except Exception:
        _close_loopback_recorder()
        return None


def _get_default_render_fft_spectrum_levels(band_count: int) -> Optional[list[int]]:
    global _loopback_smooth_levels, _loopback_agc_level, _loopback_last_time

    if _np is None or _soundcard is None or band_count <= 0:
        return None

    recorder = _get_loopback_recorder()
    if recorder is None:
        return None

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            frames = recorder.record(numframes=1024)
    except Exception:
        _close_loopback_recorder()
        return None

    if frames is None:
        return None

    mono = _np.asarray(frames, dtype=_np.float32)
    if mono.ndim > 1:
        mono = _np.mean(mono, axis=1)
    if mono.size < 64:
        return None

    mono = mono - float(_np.mean(mono))
    windowed = mono * _np.hanning(mono.size)
    spectrum = _np.abs(_np.fft.rfft(windowed))
    freqs = _np.fft.rfftfreq(mono.size, d=1.0 / 48000.0)

    band_edges = _np.geomspace(35.0, 12000.0, num=band_count + 1)
    raw_bands = [0.0] * band_count
    for i in range(band_count):
        lo = float(band_edges[i])
        hi = float(band_edges[i + 1])
        idx = _np.where((freqs >= lo) & (freqs < hi))[0]
        if idx.size <= 0:
            continue
        band = spectrum[idx]
        energy = float(_np.sqrt(_np.mean(band * band)))
        tilt = 1.0 + 1.6 * ((i / max(1, band_count - 1)) ** 1.15)
        raw_bands[i] = energy * tilt

    now = time.monotonic()
    if _loopback_last_time <= 0.0:
        dt = 1.0 / 30.0
    else:
        dt = max(0.005, min(0.250, now - _loopback_last_time))
    _loopback_last_time = now

    frame_peak = max(raw_bands) if raw_bands else 0.0
    if frame_peak >= _loopback_agc_level:
        _loopback_agc_level += (frame_peak - _loopback_agc_level) * min(1.0, dt * 9.0)
    else:
        _loopback_agc_level += (frame_peak - _loopback_agc_level) * min(1.0, dt * 1.6)
    _loopback_agc_level = max(1e-6, _loopback_agc_level)

    if len(_loopback_smooth_levels) != band_count:
        _loopback_smooth_levels = [0.0] * band_count

    levels: list[int] = [0] * band_count
    rise_rate = 16.0
    fall_per_second = 2.8
    scale = _loopback_agc_level * 1.12

    for i in range(band_count):
        target = raw_bands[i] / scale
        target = max(0.0, min(1.0, target))
        target = target ** 0.78

        prev = _loopback_smooth_levels[i]
        if target >= prev:
            nxt = prev + (target - prev) * min(1.0, dt * rise_rate)
        else:
            nxt = max(target, prev - fall_per_second * dt)

        _loopback_smooth_levels[i] = nxt
        levels[i] = max(0, min(255, int(round((nxt ** 0.85) * 255.0))))

    return levels


def get_last_render_spectrum_source() -> str:
    return _last_spectrum_source


def get_default_render_spectrum_levels(band_count: int = 16) -> Optional[list[int]]:
    """Return host-side spectrum-like levels in 0-255.

    Primary source is the Windows render endpoint peak meter (actual playback
    activity). If unavailable, it falls back to master volume scalar.
    """
    global _last_spectrum_levels, _last_spectrum_time, _last_spectrum_source

    if band_count <= 0:
        return []

    fft_levels = _get_default_render_fft_spectrum_levels(band_count)
    if fft_levels is not None:
        _last_spectrum_levels = list(fft_levels)
        _last_spectrum_time = time.monotonic()
        _last_spectrum_source = "fft"
        return fft_levels

    peak_scalar = get_default_render_peak_scalar()
    if peak_scalar is None:
        peak_scalar = get_default_render_volume_scalar()
    if peak_scalar is None:
        _last_spectrum_source = "none"
        return None

    # Honest fallback: flat level based on endpoint loudness only (no fake bins).
    flat = max(0, min(255, int(round(max(0.0, min(1.0, peak_scalar)) * 255.0))))
    levels = [flat] * band_count
    _last_spectrum_levels = list(levels)
    _last_spectrum_time = time.monotonic()
    _last_spectrum_source = "peak"
    return levels
