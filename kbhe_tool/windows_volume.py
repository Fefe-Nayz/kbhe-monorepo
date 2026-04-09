from __future__ import annotations

import ctypes
from ctypes import wintypes
from typing import Optional


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


def _get_default_audio_endpoint_volume() -> ctypes.c_void_p:
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
        get_default_audio_endpoint = WINFUNCTYPE(
            HRESULT, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)
        )(ctypes.cast(enum_vtbl[4], ctypes.c_void_p).value)
        hr = get_default_audio_endpoint(enumerator, E_RENDER, E_CONSOLE, ctypes.byref(device))
        if not _succeeded(hr):
            raise OSError(f"GetDefaultAudioEndpoint failed: 0x{ctypes.c_ulong(hr).value:08X}")

        device_vtbl = ctypes.cast(device, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        activate = WINFUNCTYPE(
            HRESULT,
            ctypes.c_void_p,
            ctypes.POINTER(GUID),
            wintypes.DWORD,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p),
        )(ctypes.cast(device_vtbl[3], ctypes.c_void_p).value)
        hr = activate(
            device,
            ctypes.byref(IID_IAudioEndpointVolume),
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


def get_default_render_volume_scalar() -> Optional[float]:
    endpoint = ctypes.c_void_p()
    initialized_here = False
    try:
        initialized_here = _coinit()
        endpoint = _get_default_audio_endpoint_volume()
        endpoint_vtbl = ctypes.cast(endpoint, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        get_scalar = WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(ctypes.c_float))(
            ctypes.cast(endpoint_vtbl[9], ctypes.c_void_p).value
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
