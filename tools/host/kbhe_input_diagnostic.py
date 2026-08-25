#!/usr/bin/env python3
"""Bounded KBHE keyboard-input diagnostic for Windows.

The live path uses Windows Raw Input and is deliberately fixed to the KBHE
runtime identity (VID 9172 / PID 0002), keyboard top-level collections, and a
20-second session.  It records numeric HID usages plus make/break transitions;
it never maps them to characters or key names.

The pure parsing helpers also support already-existing classic-PCAP USBPcap
files for the firmware's EP1 6KRO and EP4 NKRO reports.  This module never
starts USBPcapCMD and never requests elevation.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from dataclasses import dataclass, field
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import queue
import re
import struct
import sys
import threading
import time
from typing import Callable, Iterable, Iterator, Sequence


TARGET_VID = 0x9172
TARGET_PID = 0x0002
CAPTURE_SECONDS = 20.0

KEYBOARD_USAGE_PAGE = 0x07
GENERIC_DESKTOP_USAGE_PAGE = 0x01
GENERIC_DESKTOP_KEYBOARD_USAGE = 0x06

USBPCAP_LINKTYPE = 249
USBPCAP_TRANSFER_INTERRUPT = 1
USBPCAP_EP_6KRO = 0x81
USBPCAP_EP_NKRO = 0x84

RI_KEY_BREAK = 0x0001
RI_KEY_E0 = 0x0002
RI_KEY_E1 = 0x0004


class DiagnosticError(RuntimeError):
    """Expected diagnostic failure with a user-facing message."""


@dataclass(frozen=True)
class UsageTransition:
    """One numeric keyboard-usage transition; never a text character."""

    t_ms: float
    usage: int | None
    state: str
    source: str
    scan_code: str | None = None

    def as_dict(self) -> dict[str, object]:
        return {
            "t_ms": round(self.t_ms, 3),
            "hid_usage": format_usage(self.usage),
            "state": self.state,
            "source": self.source,
            "scan_code": self.scan_code,
        }


@dataclass
class DiagnosticResult:
    """Result of one explicitly bounded session."""

    layer: str
    started_at_utc: str
    completed_at_utc: str
    duration_s: float
    device_collections: int
    events: list[UsageTransition] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def summary(self) -> list[dict[str, object]]:
        buckets: dict[int | None, dict[str, object]] = {}
        for event in self.events:
            bucket = buckets.setdefault(
                event.usage,
                {
                    "hid_usage": format_usage(event.usage),
                    "make": 0,
                    "break": 0,
                    "first_ms": event.t_ms,
                    "last_ms": event.t_ms,
                },
            )
            bucket[event.state] = int(bucket[event.state]) + 1
            bucket["first_ms"] = min(float(bucket["first_ms"]), event.t_ms)
            bucket["last_ms"] = max(float(bucket["last_ms"]), event.t_ms)

        def order(item: tuple[int | None, dict[str, object]]) -> int:
            usage, _bucket = item
            return 0x10000 if usage is None else usage

        result: list[dict[str, object]] = []
        for _usage, bucket in sorted(buckets.items(), key=order):
            copy = dict(bucket)
            copy["first_ms"] = round(float(copy["first_ms"]), 3)
            copy["last_ms"] = round(float(copy["last_ms"]), 3)
            result.append(copy)
        return result

    def as_dict(self, *, include_events: bool) -> dict[str, object]:
        document: dict[str, object] = {
            "schema": "kbhe-keyboard-diagnostic/v1",
            "layer": self.layer,
            "target": {
                "vid": f"0x{TARGET_VID:04X}",
                "pid": f"0x{TARGET_PID:04X}",
                "usage_page": f"0x{GENERIC_DESKTOP_USAGE_PAGE:02X}",
                "usage": f"0x{GENERIC_DESKTOP_KEYBOARD_USAGE:02X}",
            },
            "started_at_utc": self.started_at_utc,
            "completed_at_utc": self.completed_at_utc,
            "duration_s": round(self.duration_s, 3),
            "device_collections": self.device_collections,
            "event_count": len(self.events),
            "summary": self.summary(),
            "warnings": list(self.warnings),
        }
        if include_events:
            document["events"] = [event.as_dict() for event in self.events]
        return document


def format_usage(usage: int | None) -> str | None:
    """Return a numeric HID usage without assigning a character or key name."""

    if usage is None:
        return None
    return f"0x{usage:02X}"


def _modifier_usages(mask: int) -> set[int]:
    return {0xE0 + bit for bit in range(8) if mask & (1 << bit)}


def parse_6kro_report(report: bytes) -> frozenset[int]:
    """Decode a boot-keyboard report into numeric keyboard-page usages."""

    if len(report) < 8:
        raise ValueError(f"6KRO report must contain at least 8 bytes, got {len(report)}")
    usages = _modifier_usages(report[0])
    # 0 is "no event"; 1..3 are HID rollover/error indicators, not keys.
    usages.update(usage for usage in report[2:8] if usage >= 4)
    return frozenset(usages)


def parse_nkro_report(report: bytes) -> frozenset[int]:
    """Decode the KBHE 17-byte NKRO bitmap into numeric keyboard usages."""

    if len(report) < 17:
        raise ValueError(f"NKRO report must contain at least 17 bytes, got {len(report)}")
    usages = _modifier_usages(report[0])
    for byte_index, value in enumerate(report[1:17]):
        for bit in range(8):
            usage = byte_index * 8 + bit
            if usage != 0 and value & (1 << bit):
                usages.add(usage)
    return frozenset(usages)


def diff_usage_sets(
    previous: Iterable[int], current: Iterable[int]
) -> list[tuple[int, str]]:
    """Return deterministic break-then-make transitions between two reports."""

    before = set(previous)
    after = set(current)
    transitions = [(usage, "break") for usage in sorted(before - after)]
    transitions.extend((usage, "make") for usage in sorted(after - before))
    return transitions


class UsbReportTracker:
    """Stateful EP1/EP4 parser for an existing USBPcap stream."""

    def __init__(self) -> None:
        self._pressed: dict[int, frozenset[int]] = {
            USBPCAP_EP_6KRO: frozenset(),
            USBPCAP_EP_NKRO: frozenset(),
        }

    def feed(self, endpoint: int, payload: bytes, t_ms: float) -> list[UsageTransition]:
        if endpoint == USBPCAP_EP_6KRO:
            current = parse_6kro_report(payload)
            source = "EP1-6KRO"
        elif endpoint == USBPCAP_EP_NKRO:
            current = parse_nkro_report(payload)
            source = "EP4-NKRO"
        else:
            raise ValueError(f"unsupported KBHE keyboard endpoint 0x{endpoint:02X}")

        previous = self._pressed[endpoint]
        self._pressed[endpoint] = current
        return [
            UsageTransition(t_ms=t_ms, usage=usage, state=state, source=source)
            for usage, state in diff_usage_sets(previous, current)
        ]


@dataclass(frozen=True)
class UsbPcapInterruptRecord:
    timestamp_s: float
    device_address: int
    endpoint: int
    payload: bytes


def _pcap_format(global_header: bytes) -> tuple[str, float, int]:
    if len(global_header) != 24:
        raise DiagnosticError("truncated classic-PCAP global header")
    magic = global_header[:4]
    formats = {
        b"\xd4\xc3\xb2\xa1": ("<", 1_000_000.0),
        b"\xa1\xb2\xc3\xd4": (">", 1_000_000.0),
        b"\x4d\x3c\xb2\xa1": ("<", 1_000_000_000.0),
        b"\xa1\xb2\x3c\x4d": (">", 1_000_000_000.0),
    }
    try:
        endian, fraction_scale = formats[magic]
    except KeyError as exc:
        if magic == b"\x0a\x0d\x0d\x0a":
            raise DiagnosticError(
                "PCAPNG is not supported by the dependency-free reader; export as classic PCAP"
            ) from exc
        raise DiagnosticError("unrecognized PCAP magic") from exc
    linktype = struct.unpack(endian + "I", global_header[20:24])[0]
    return endian, fraction_scale, linktype


def iter_usbpcap_interrupt_records(
    capture_path: str | os.PathLike[str], *, device_address: int
) -> Iterator[UsbPcapInterruptRecord]:
    """Yield only EP1/EP4 interrupt responses for one explicit USB address."""

    path = Path(capture_path)
    with path.open("rb") as stream:
        endian, fraction_scale, linktype = _pcap_format(stream.read(24))
        if linktype != USBPCAP_LINKTYPE:
            raise DiagnosticError(
                f"expected LINKTYPE_USBPCAP ({USBPCAP_LINKTYPE}), got {linktype}"
            )

        record_struct = struct.Struct(endian + "IIII")
        while True:
            record_header = stream.read(record_struct.size)
            if not record_header:
                return
            if len(record_header) != record_struct.size:
                raise DiagnosticError("truncated PCAP record header")
            ts_seconds, ts_fraction, captured_length, _original_length = (
                record_struct.unpack(record_header)
            )
            packet = stream.read(captured_length)
            if len(packet) != captured_length:
                raise DiagnosticError("truncated PCAP packet")
            if len(packet) < 27:
                continue

            # USBPcap's packed base header is always little-endian, independent
            # of the enclosing PCAP byte order.
            header_length = struct.unpack_from("<H", packet, 0)[0]
            status = struct.unpack_from("<I", packet, 10)[0]
            info = packet[16]
            packet_device = struct.unpack_from("<H", packet, 19)[0]
            endpoint = packet[21]
            transfer = packet[22]
            data_length = struct.unpack_from("<I", packet, 23)[0]

            # Filter identity and endpoint before looking at report payload.
            if packet_device != device_address:
                continue
            if endpoint not in (USBPCAP_EP_6KRO, USBPCAP_EP_NKRO):
                continue
            if transfer != USBPCAP_TRANSFER_INTERRUPT:
                continue
            if not (info & 0x01) or status != 0 or data_length == 0:
                continue
            if header_length < 27 or header_length > len(packet):
                raise DiagnosticError("invalid USBPcap pseudo-header length")
            payload_end = header_length + data_length
            if payload_end > len(packet):
                raise DiagnosticError("truncated USBPcap interrupt payload")
            yield UsbPcapInterruptRecord(
                timestamp_s=ts_seconds + ts_fraction / fraction_scale,
                device_address=packet_device,
                endpoint=endpoint,
                payload=packet[header_length:payload_end],
            )


def analyze_usbpcap(
    capture_path: str | os.PathLike[str], *, device_address: int
) -> DiagnosticResult:
    """Analyze one already-existing USBPcap file without launching a capture."""

    if not 1 <= device_address <= 127:
        raise DiagnosticError("USB device address must be between 1 and 127")
    records = iter_usbpcap_interrupt_records(
        capture_path, device_address=device_address
    )
    tracker = UsbReportTracker()
    events: list[UsageTransition] = []
    first_timestamp: float | None = None
    last_timestamp: float | None = None
    for record in records:
        if first_timestamp is None:
            first_timestamp = record.timestamp_s
        last_timestamp = record.timestamp_s
        events.extend(
            tracker.feed(
                record.endpoint,
                record.payload,
                (record.timestamp_s - first_timestamp) * 1000.0,
            )
        )
    if first_timestamp is None or last_timestamp is None:
        raise DiagnosticError(
            "no successful EP1/EP4 interrupt response found for that USB address"
        )
    started = datetime.fromtimestamp(first_timestamp, timezone.utc)
    completed = datetime.fromtimestamp(last_timestamp, timezone.utc)
    return DiagnosticResult(
        layer="USBPcap offline URB payloads (existing capture; no capture started)",
        started_at_utc=started.isoformat(),
        completed_at_utc=completed.isoformat(),
        duration_s=max(0.0, last_timestamp - first_timestamp),
        device_collections=2,
        events=events,
    )


# IBM/PC set-1 scan codes as exposed by Windows Raw Input, mapped to USB HID
# Keyboard/Keypad page usages.  Mapping is physical and never produces text.
_BASE_SCAN_TO_USAGE: dict[int, int] = {
    0x01: 0x29,
    0x02: 0x1E,
    0x03: 0x1F,
    0x04: 0x20,
    0x05: 0x21,
    0x06: 0x22,
    0x07: 0x23,
    0x08: 0x24,
    0x09: 0x25,
    0x0A: 0x26,
    0x0B: 0x27,
    0x0C: 0x2D,
    0x0D: 0x2E,
    0x0E: 0x2A,
    0x0F: 0x2B,
    0x10: 0x14,
    0x11: 0x1A,
    0x12: 0x08,
    0x13: 0x15,
    0x14: 0x17,
    0x15: 0x1C,
    0x16: 0x18,
    0x17: 0x0C,
    0x18: 0x12,
    0x19: 0x13,
    0x1A: 0x2F,
    0x1B: 0x30,
    0x1C: 0x28,
    0x1D: 0xE0,
    0x1E: 0x04,
    0x1F: 0x16,
    0x20: 0x07,
    0x21: 0x09,
    0x22: 0x0A,
    0x23: 0x0B,
    0x24: 0x0D,
    0x25: 0x0E,
    0x26: 0x0F,
    0x27: 0x33,
    0x28: 0x34,
    0x29: 0x35,
    0x2A: 0xE1,
    0x2B: 0x31,
    0x2C: 0x1D,
    0x2D: 0x1B,
    0x2E: 0x06,
    0x2F: 0x19,
    0x30: 0x05,
    0x31: 0x11,
    0x32: 0x10,
    0x33: 0x36,
    0x34: 0x37,
    0x35: 0x38,
    0x36: 0xE5,
    0x37: 0x55,
    0x38: 0xE2,
    0x39: 0x2C,
    0x3A: 0x39,
    0x3B: 0x3A,
    0x3C: 0x3B,
    0x3D: 0x3C,
    0x3E: 0x3D,
    0x3F: 0x3E,
    0x40: 0x3F,
    0x41: 0x40,
    0x42: 0x41,
    0x43: 0x42,
    0x44: 0x43,
    0x45: 0x53,
    0x46: 0x47,
    0x47: 0x5F,
    0x48: 0x60,
    0x49: 0x61,
    0x4A: 0x56,
    0x4B: 0x5C,
    0x4C: 0x5D,
    0x4D: 0x5E,
    0x4E: 0x57,
    0x4F: 0x59,
    0x50: 0x5A,
    0x51: 0x5B,
    0x52: 0x62,
    0x53: 0x63,
    0x56: 0x64,
    0x57: 0x44,
    0x58: 0x45,
}

_E0_SCAN_TO_USAGE: dict[int, int] = {
    0x1C: 0x58,
    0x1D: 0xE4,
    0x35: 0x54,
    0x37: 0x46,
    0x38: 0xE6,
    0x47: 0x4A,
    0x48: 0x52,
    0x49: 0x4B,
    0x4B: 0x50,
    0x4D: 0x4F,
    0x4F: 0x4D,
    0x50: 0x51,
    0x51: 0x4E,
    0x52: 0x49,
    0x53: 0x4C,
    0x5B: 0xE3,
    0x5C: 0xE7,
    0x5D: 0x65,
    0x5E: 0x66,
    0x5F: 0x82,
    0x63: 0x83,
}

# F13..F24 are most reliably identified by their virtual-key value because
# their scan-code assignments vary between Windows keyboard layouts/drivers.
_VKEY_TO_USAGE = {0x7C + offset: 0x68 + offset for offset in range(12)}
_VKEY_TO_USAGE.update({0x13: 0x48})  # VK_PAUSE

_TARGET_PATH_PATTERN = re.compile(
    r"(?:^|[#\\])VID_9172&PID_0002(?:&|#)", re.IGNORECASE
)


def is_target_device_path(path: str) -> bool:
    """Match the exact fixed runtime VID/PID segment in a Windows HID path."""

    return bool(_TARGET_PATH_PATTERN.search(path))


def raw_input_usage(make_code: int, flags: int, vkey: int) -> int | None:
    """Map a Windows Raw Input scan code to a numeric HID keyboard usage."""

    make_code &= 0xFF
    if vkey == 0xFF:  # KEYBOARD_OVERRUN_MAKE_CODE / invalid raw key
        return None
    if vkey in _VKEY_TO_USAGE:
        return _VKEY_TO_USAGE[vkey]
    if flags & RI_KEY_E1:
        return 0x48 if vkey == 0x13 else None
    if flags & RI_KEY_E0:
        # Windows may expose synthetic E0 shifts around Print Screen.  They do
        # not correspond to independent HID transitions.
        if make_code in (0x2A, 0x36):
            return None
        return _E0_SCAN_TO_USAGE.get(make_code)
    return _BASE_SCAN_TO_USAGE.get(make_code)


def format_scan_code(make_code: int, flags: int) -> str:
    if flags & RI_KEY_E1:
        prefix = "E1-"
    elif flags & RI_KEY_E0:
        prefix = "E0-"
    else:
        prefix = ""
    return f"{prefix}{make_code & 0xFF:02X}"


if os.name == "nt":
    LRESULT = ctypes.c_ssize_t
    UINT_PTR = ctypes.c_size_t
    WNDPROC = ctypes.WINFUNCTYPE(
        LRESULT, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
    )

    class RAWINPUTDEVICELIST(ctypes.Structure):
        _fields_ = [("hDevice", wintypes.HANDLE), ("dwType", wintypes.DWORD)]

    class RAWINPUTDEVICE(ctypes.Structure):
        _fields_ = [
            ("usUsagePage", wintypes.USHORT),
            ("usUsage", wintypes.USHORT),
            ("dwFlags", wintypes.DWORD),
            ("hwndTarget", wintypes.HWND),
        ]

    class RAWINPUTHEADER(ctypes.Structure):
        _fields_ = [
            ("dwType", wintypes.DWORD),
            ("dwSize", wintypes.DWORD),
            ("hDevice", wintypes.HANDLE),
            ("wParam", wintypes.WPARAM),
        ]

    class RAWKEYBOARD(ctypes.Structure):
        _fields_ = [
            ("MakeCode", wintypes.USHORT),
            ("Flags", wintypes.USHORT),
            ("Reserved", wintypes.USHORT),
            ("VKey", wintypes.USHORT),
            ("Message", wintypes.UINT),
            ("ExtraInformation", wintypes.ULONG),
        ]

    class WNDCLASSW(ctypes.Structure):
        _fields_ = [
            ("style", wintypes.UINT),
            ("lpfnWndProc", WNDPROC),
            ("cbClsExtra", ctypes.c_int),
            ("cbWndExtra", ctypes.c_int),
            ("hInstance", wintypes.HINSTANCE),
            ("hIcon", wintypes.HICON),
            ("hCursor", wintypes.HANDLE),
            ("hbrBackground", wintypes.HBRUSH),
            ("lpszMenuName", wintypes.LPCWSTR),
            ("lpszClassName", wintypes.LPCWSTR),
        ]


RIM_TYPEKEYBOARD = 1
RIDI_DEVICENAME = 0x20000007
RID_INPUT = 0x10000003
RIDEV_REMOVE = 0x00000001
RIDEV_INPUTSINK = 0x00000100
RIDEV_DEVNOTIFY = 0x00002000
WM_INPUT = 0x00FF
WM_INPUT_DEVICE_CHANGE = 0x00FE
WM_TIMER = 0x0113
WM_CLOSE = 0x0010
WM_DESTROY = 0x0002
UINT_ERROR = 0xFFFFFFFF


def _win32_functions() -> tuple[object, object]:
    if os.name != "nt":
        raise DiagnosticError("live Raw Input capture is only available on Windows")
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    user32.GetRawInputDeviceList.argtypes = [
        ctypes.POINTER(RAWINPUTDEVICELIST),
        ctypes.POINTER(wintypes.UINT),
        wintypes.UINT,
    ]
    user32.GetRawInputDeviceList.restype = wintypes.UINT
    user32.GetRawInputDeviceInfoW.argtypes = [
        wintypes.HANDLE,
        wintypes.UINT,
        wintypes.LPVOID,
        ctypes.POINTER(wintypes.UINT),
    ]
    user32.GetRawInputDeviceInfoW.restype = wintypes.UINT
    user32.RegisterRawInputDevices.argtypes = [
        ctypes.POINTER(RAWINPUTDEVICE),
        wintypes.UINT,
        wintypes.UINT,
    ]
    user32.RegisterRawInputDevices.restype = wintypes.BOOL
    user32.GetRawInputData.argtypes = [
        wintypes.HANDLE,
        wintypes.UINT,
        wintypes.LPVOID,
        ctypes.POINTER(wintypes.UINT),
        wintypes.UINT,
    ]
    user32.GetRawInputData.restype = wintypes.UINT
    user32.RegisterClassW.argtypes = [ctypes.POINTER(WNDCLASSW)]
    user32.RegisterClassW.restype = wintypes.ATOM
    user32.UnregisterClassW.argtypes = [wintypes.LPCWSTR, wintypes.HINSTANCE]
    user32.UnregisterClassW.restype = wintypes.BOOL
    user32.CreateWindowExW.argtypes = [
        wintypes.DWORD,
        wintypes.LPCWSTR,
        wintypes.LPCWSTR,
        wintypes.DWORD,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        wintypes.HWND,
        wintypes.HMENU,
        wintypes.HINSTANCE,
        wintypes.LPVOID,
    ]
    user32.CreateWindowExW.restype = wintypes.HWND
    user32.DefWindowProcW.argtypes = [
        wintypes.HWND,
        wintypes.UINT,
        wintypes.WPARAM,
        wintypes.LPARAM,
    ]
    user32.DefWindowProcW.restype = LRESULT
    user32.DestroyWindow.argtypes = [wintypes.HWND]
    user32.DestroyWindow.restype = wintypes.BOOL
    user32.PostQuitMessage.argtypes = [ctypes.c_int]
    user32.GetMessageW.argtypes = [
        ctypes.POINTER(wintypes.MSG),
        wintypes.HWND,
        wintypes.UINT,
        wintypes.UINT,
    ]
    user32.GetMessageW.restype = wintypes.BOOL
    user32.TranslateMessage.argtypes = [ctypes.POINTER(wintypes.MSG)]
    user32.DispatchMessageW.argtypes = [ctypes.POINTER(wintypes.MSG)]
    user32.SetTimer.argtypes = [
        wintypes.HWND,
        UINT_PTR,
        wintypes.UINT,
        wintypes.LPVOID,
    ]
    user32.SetTimer.restype = UINT_PTR
    user32.KillTimer.argtypes = [wintypes.HWND, UINT_PTR]
    kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
    kernel32.GetModuleHandleW.restype = wintypes.HMODULE
    return user32, kernel32


def _device_name(user32: object, handle: int) -> str:
    size = wintypes.UINT(0)
    result = user32.GetRawInputDeviceInfoW(
        wintypes.HANDLE(handle), RIDI_DEVICENAME, None, ctypes.byref(size)
    )
    if result == UINT_ERROR or size.value == 0:
        return ""
    buffer = ctypes.create_unicode_buffer(size.value + 1)
    result = user32.GetRawInputDeviceInfoW(
        wintypes.HANDLE(handle), RIDI_DEVICENAME, buffer, ctypes.byref(size)
    )
    if result == UINT_ERROR:
        return ""
    return buffer.value


def discover_target_raw_input_devices() -> dict[int, str]:
    """Return only KBHE keyboard collection handles, labeled by interface."""

    user32, _kernel32 = _win32_functions()
    count = wintypes.UINT(0)
    if (
        user32.GetRawInputDeviceList(
            None, ctypes.byref(count), ctypes.sizeof(RAWINPUTDEVICELIST)
        )
        == UINT_ERROR
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    if count.value == 0:
        return {}
    devices = (RAWINPUTDEVICELIST * count.value)()
    result = user32.GetRawInputDeviceList(
        devices, ctypes.byref(count), ctypes.sizeof(RAWINPUTDEVICELIST)
    )
    if result == UINT_ERROR:
        raise ctypes.WinError(ctypes.get_last_error())

    matches: list[tuple[int, str]] = []
    for device in devices[: result]:
        if device.dwType != RIM_TYPEKEYBOARD:
            continue
        handle = int(device.hDevice or 0)
        path = _device_name(user32, handle)
        if is_target_device_path(path):
            matches.append((handle, path))

    labels: dict[int, str] = {}
    for index, (handle, path) in enumerate(sorted(matches, key=lambda pair: pair[1]), 1):
        interface_match = re.search(r"&MI_([0-9A-F]{2})(?:&|#)", path, re.I)
        interface = (
            f"MI_{interface_match.group(1).upper()}"
            if interface_match
            else f"keyboard-collection-{index}"
        )
        labels[handle] = interface
    return labels


class _TransitionGate:
    """Suppress Raw Input auto-repeat while retaining boundary anomalies."""

    def __init__(self) -> None:
        self.pressed: dict[str, set[int]] = {}
        self.duplicate_make = 0
        self.orphan_break = 0

    def accept(self, source: str, usage: int | None, state: str) -> bool:
        if usage is None:
            return True
        pressed = self.pressed.setdefault(source, set())
        if state == "make":
            if usage in pressed:
                self.duplicate_make += 1
                return False
            pressed.add(usage)
            return True
        if usage not in pressed:
            self.orphan_break += 1
            return True
        pressed.remove(usage)
        return True


def capture_raw_input(
    *,
    on_event: Callable[[UsageTransition], None] | None = None,
    cancel_event: threading.Event | None = None,
) -> DiagnosticResult:
    """Capture the fixed KBHE target for exactly (at most) 20 seconds."""

    user32, kernel32 = _win32_functions()
    target_devices = discover_target_raw_input_devices()
    if not target_devices:
        raise DiagnosticError(
            "KBHE VID_9172/PID_0002 keyboard collection not found in Raw Input"
        )

    events: list[UsageTransition] = []
    warnings: list[str] = []
    gate = _TransitionGate()
    started_utc = datetime.now(timezone.utc)
    start_clock = time.perf_counter()
    stopped = False
    class_name = f"KBHERawInputDiagnostic_{os.getpid()}_{threading.get_ident()}"
    hinstance = kernel32.GetModuleHandleW(None)

    def refresh_target_devices() -> None:
        nonlocal target_devices
        try:
            refreshed = discover_target_raw_input_devices()
            if refreshed:
                target_devices = refreshed
        except Exception as exc:  # keep the bounded session alive
            warnings.append(f"target refresh failed: {exc}")

    @WNDPROC
    def window_proc(hwnd: int, message: int, wparam: int, lparam: int) -> int:
        nonlocal stopped
        try:
            if message == WM_INPUT:
                size = wintypes.UINT(0)
                result = user32.GetRawInputData(
                    wintypes.HANDLE(lparam),
                    RID_INPUT,
                    None,
                    ctypes.byref(size),
                    ctypes.sizeof(RAWINPUTHEADER),
                )
                if result == UINT_ERROR or size.value < (
                    ctypes.sizeof(RAWINPUTHEADER) + ctypes.sizeof(RAWKEYBOARD)
                ):
                    return user32.DefWindowProcW(hwnd, message, wparam, lparam)
                raw = ctypes.create_string_buffer(size.value)
                copied = user32.GetRawInputData(
                    wintypes.HANDLE(lparam),
                    RID_INPUT,
                    raw,
                    ctypes.byref(size),
                    ctypes.sizeof(RAWINPUTHEADER),
                )
                if copied == UINT_ERROR:
                    return user32.DefWindowProcW(hwnd, message, wparam, lparam)
                header = RAWINPUTHEADER.from_buffer_copy(
                    raw.raw[: ctypes.sizeof(RAWINPUTHEADER)]
                )
                source = target_devices.get(int(header.hDevice or 0))
                # This is the fail-closed boundary: data from every other Raw
                # Input device is discarded before its key structure is read.
                if header.dwType != RIM_TYPEKEYBOARD or source is None:
                    return user32.DefWindowProcW(hwnd, message, wparam, lparam)
                offset = ctypes.sizeof(RAWINPUTHEADER)
                keyboard = RAWKEYBOARD.from_buffer_copy(
                    raw.raw[offset : offset + ctypes.sizeof(RAWKEYBOARD)]
                )
                elapsed = time.perf_counter() - start_clock
                if elapsed > CAPTURE_SECONDS:
                    return user32.DefWindowProcW(hwnd, message, wparam, lparam)
                usage = raw_input_usage(
                    keyboard.MakeCode, keyboard.Flags, keyboard.VKey
                )
                state = "break" if keyboard.Flags & RI_KEY_BREAK else "make"
                scan_code = format_scan_code(keyboard.MakeCode, keyboard.Flags)
                if usage is None:
                    # Synthetic Print-Screen shifts are intentionally ignored;
                    # other unknown target scan codes remain numeric diagnostics.
                    if keyboard.Flags & RI_KEY_E0 and keyboard.MakeCode in (0x2A, 0x36):
                        return user32.DefWindowProcW(hwnd, message, wparam, lparam)
                    warnings.append(f"unmapped target scan code {scan_code}")
                if gate.accept(source, usage, state):
                    event = UsageTransition(
                        t_ms=elapsed * 1000.0,
                        usage=usage,
                        state=state,
                        source=source,
                        scan_code=scan_code,
                    )
                    events.append(event)
                    if on_event is not None:
                        on_event(event)
                return user32.DefWindowProcW(hwnd, message, wparam, lparam)

            if message == WM_INPUT_DEVICE_CHANGE:
                refresh_target_devices()
                return 0
            if message == WM_TIMER:
                if (
                    time.perf_counter() - start_clock >= CAPTURE_SECONDS
                    or (cancel_event is not None and cancel_event.is_set())
                ):
                    stopped = True
                    user32.DestroyWindow(hwnd)
                return 0
            if message == WM_CLOSE:
                stopped = True
                user32.DestroyWindow(hwnd)
                return 0
            if message == WM_DESTROY:
                user32.PostQuitMessage(0)
                return 0
        except Exception as exc:  # WNDPROC exceptions cannot cross Win32
            warnings.append(f"Raw Input callback error: {exc}")
        return user32.DefWindowProcW(hwnd, message, wparam, lparam)

    window_class = WNDCLASSW()
    window_class.lpfnWndProc = window_proc
    window_class.hInstance = hinstance
    window_class.lpszClassName = class_name
    if not user32.RegisterClassW(ctypes.byref(window_class)):
        raise ctypes.WinError(ctypes.get_last_error())

    hwnd = None
    registered = False
    try:
        # HWND_MESSAGE (-3) creates a non-visible, message-only window.  The
        # separate .pyw GUI remains the explicit user-visible session surface.
        hwnd_message = wintypes.HWND(-3)
        hwnd = user32.CreateWindowExW(
            0,
            class_name,
            class_name,
            0,
            0,
            0,
            0,
            0,
            hwnd_message,
            None,
            hinstance,
            None,
        )
        if not hwnd:
            raise ctypes.WinError(ctypes.get_last_error())
        registration = RAWINPUTDEVICE(
            GENERIC_DESKTOP_USAGE_PAGE,
            GENERIC_DESKTOP_KEYBOARD_USAGE,
            RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,
            hwnd,
        )
        if not user32.RegisterRawInputDevices(
            ctypes.byref(registration), 1, ctypes.sizeof(RAWINPUTDEVICE)
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        registered = True
        if not user32.SetTimer(hwnd, 1, 25, None):
            raise ctypes.WinError(ctypes.get_last_error())

        message = wintypes.MSG()
        while not stopped:
            result = user32.GetMessageW(ctypes.byref(message), None, 0, 0)
            if result == -1:
                raise ctypes.WinError(ctypes.get_last_error())
            if result == 0:
                break
            user32.TranslateMessage(ctypes.byref(message))
            user32.DispatchMessageW(ctypes.byref(message))
    finally:
        if hwnd:
            user32.KillTimer(hwnd, 1)
        if registered:
            removal = RAWINPUTDEVICE(
                GENERIC_DESKTOP_USAGE_PAGE,
                GENERIC_DESKTOP_KEYBOARD_USAGE,
                RIDEV_REMOVE,
                None,
            )
            user32.RegisterRawInputDevices(
                ctypes.byref(removal), 1, ctypes.sizeof(RAWINPUTDEVICE)
            )
        user32.UnregisterClassW(class_name, hinstance)

    if gate.duplicate_make:
        warnings.append(
            f"suppressed {gate.duplicate_make} repeated make event(s) (Raw Input repeat)"
        )
    if gate.orphan_break:
        warnings.append(
            f"observed {gate.orphan_break} break event(s) without a make in this session"
        )
    completed_utc = datetime.now(timezone.utc)
    elapsed_s = min(time.perf_counter() - start_clock, CAPTURE_SECONDS)
    return DiagnosticResult(
        layer="Windows Raw Input (post-HID keyboard stack; not USB URB packets)",
        started_at_utc=started_utc.isoformat(),
        completed_at_utc=completed_utc.isoformat(),
        duration_s=elapsed_s,
        device_collections=len(target_devices),
        events=events,
        warnings=list(dict.fromkeys(warnings)),
    )


def _write_json(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )


def run_gui() -> int:
    """Run an explicit GUI session; the ordered numeric stream stays in memory."""

    if os.name != "nt":
        raise DiagnosticError("the Raw Input GUI is only available on Windows")
    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    root.title("KBHE — diagnostic clavier Raw Input")
    root.geometry("820x590")
    root.minsize(720, 500)

    event_queue: queue.Queue[tuple[str, object]] = queue.Queue()
    cancel = threading.Event()
    started_clock: list[float | None] = [None]
    result_holder: list[DiagnosticResult | None] = [None]

    outer = ttk.Frame(root, padding=14)
    outer.pack(fill="both", expand=True)
    ttk.Label(
        outer,
        text="Diagnostic KBHE ciblé — VID 9172 / PID 0002",
        font=("Segoe UI", 14, "bold"),
    ).pack(anchor="w")
    ttk.Label(
        outer,
        text=(
            "Capture explicite de 20 s. Couche Windows Raw Input post-HID (pas URB). "
            "Seuls les usages HID numériques make/break du KBHE sont traités; "
            "aucun caractère n'est reconstruit."
        ),
        wraplength=780,
        justify="left",
    ).pack(anchor="w", pady=(4, 10))

    status = tk.StringVar(value="Relâchez les touches, puis démarrez la session.")
    ttk.Label(outer, textvariable=status).pack(anchor="w")
    progress = ttk.Progressbar(outer, maximum=CAPTURE_SECONDS, value=0)
    progress.pack(fill="x", pady=(5, 10))

    notebook = ttk.Notebook(outer)
    notebook.pack(fill="both", expand=True)
    events_tab = ttk.Frame(notebook, padding=4)
    summary_tab = ttk.Frame(notebook, padding=4)
    notebook.add(events_tab, text="Ordre brut (éphémère)")
    notebook.add(summary_tab, text="Résumé par usage")

    columns = ("time", "usage", "state", "scan", "source")
    events_view = ttk.Treeview(
        events_tab, columns=columns, show="headings", height=14
    )
    headings = {
        "time": "t (ms)",
        "usage": "Usage HID",
        "state": "Transition",
        "scan": "Scancode",
        "source": "Collection",
    }
    widths = {"time": 110, "usage": 110, "state": 100, "scan": 100, "source": 160}
    for column in columns:
        events_view.heading(column, text=headings[column])
        events_view.column(column, width=widths[column], anchor="center")
    events_view.pack(fill="both", expand=True)

    summary_columns = ("usage", "make", "break", "first", "last")
    summary_view = ttk.Treeview(
        summary_tab, columns=summary_columns, show="headings", height=14
    )
    summary_headings = {
        "usage": "Usage HID",
        "make": "Make",
        "break": "Break",
        "first": "Premier (ms)",
        "last": "Dernier (ms)",
    }
    for column in summary_columns:
        summary_view.heading(column, text=summary_headings[column])
        summary_view.column(column, width=130, anchor="center")
    summary_view.pack(fill="both", expand=True)

    button_row = ttk.Frame(outer)
    button_row.pack(fill="x", pady=(10, 0))
    start_button = ttk.Button(button_row, text="Démarrer 20 s")
    start_button.pack(side="left")
    copy_button = ttk.Button(button_row, text="Copier le résumé", state="disabled")
    copy_button.pack(side="left", padx=(8, 0))
    close_button = ttk.Button(button_row, text="Fermer")
    close_button.pack(side="right")

    def worker() -> None:
        try:
            result = capture_raw_input(
                on_event=lambda event: event_queue.put(("event", event)),
                cancel_event=cancel,
            )
            event_queue.put(("done", result))
        except Exception as exc:
            event_queue.put(("error", exc))

    def start() -> None:
        if started_clock[0] is not None:
            return
        events_view.delete(*events_view.get_children())
        summary_view.delete(*summary_view.get_children())
        notebook.select(events_tab)
        start_button.configure(state="disabled")
        copy_button.configure(state="disabled")
        status.set("Capture active — 20,0 s restantes")
        started_clock[0] = time.perf_counter()
        threading.Thread(target=worker, daemon=True, name="kbhe-raw-input").start()

    def copy_summary() -> None:
        result = result_holder[0]
        if result is None:
            return
        # Only the aggregate is copied.  The ordered event list is deliberately
        # ephemeral and disappears when this window closes.
        document = result.as_dict(include_events=False)
        root.clipboard_clear()
        root.clipboard_append(json.dumps(document, indent=2, ensure_ascii=True))
        status.set("Résumé agrégé copié; l'ordre brut reste uniquement dans cette fenêtre.")

    def poll() -> None:
        try:
            while True:
                kind, payload = event_queue.get_nowait()
                if kind == "event":
                    event = payload
                    assert isinstance(event, UsageTransition)
                    events_view.insert(
                        "",
                        "end",
                        values=(
                            f"{event.t_ms:.3f}",
                            format_usage(event.usage) or "inconnu",
                            event.state,
                            event.scan_code or "",
                            event.source,
                        ),
                    )
                    events_view.yview_moveto(1.0)
                elif kind == "done":
                    result = payload
                    assert isinstance(result, DiagnosticResult)
                    result_holder[0] = result
                    for bucket in result.summary():
                        summary_view.insert(
                            "",
                            "end",
                            values=(
                                bucket["hid_usage"] or "inconnu",
                                bucket["make"],
                                bucket["break"],
                                f"{float(bucket['first_ms']):.3f}",
                                f"{float(bucket['last_ms']):.3f}",
                            ),
                        )
                    progress.configure(value=CAPTURE_SECONDS)
                    status.set(
                        f"Terminé: {len(result.events)} transitions, "
                        f"{len(result.summary())} usages distincts."
                    )
                    copy_button.configure(state="normal")
                else:
                    status.set(f"Erreur: {payload}")
                    start_button.configure(state="normal")
                    started_clock[0] = None
        except queue.Empty:
            pass

        if started_clock[0] is not None and result_holder[0] is None:
            elapsed = min(time.perf_counter() - started_clock[0], CAPTURE_SECONDS)
            progress.configure(value=elapsed)
            status.set(f"Capture active — {max(0.0, CAPTURE_SECONDS - elapsed):.1f} s restantes")
        root.after(25, poll)

    def close() -> None:
        cancel.set()
        root.destroy()

    start_button.configure(command=start)
    copy_button.configure(command=copy_summary)
    close_button.configure(command=close)
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(25, poll)
    root.mainloop()
    return 0


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "20-second numeric KBHE keyboard diagnostic; fixed to VID 9172/PID 0002"
        )
    )
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser("gui", help="visible 20-second Raw Input session")

    capture = subparsers.add_parser(
        "capture", help="20-second Raw Input session (console/automation)"
    )
    capture.add_argument(
        "--output",
        type=Path,
        help="explicit JSON destination (numeric events only); stdout if omitted",
    )

    offline = subparsers.add_parser(
        "usbpcap", help="read an existing classic-PCAP USBPcap file; never captures"
    )
    offline.add_argument("capture_file", type=Path)
    offline.add_argument(
        "--device-address",
        type=int,
        required=True,
        help="USB address already verified as the KBHE (for example 64)",
    )
    offline.add_argument("--output", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    command = args.command or "gui"
    try:
        if command == "gui":
            return run_gui()
        if command == "capture":
            result = capture_raw_input()
        elif command == "usbpcap":
            result = analyze_usbpcap(
                args.capture_file, device_address=args.device_address
            )
        else:  # pragma: no cover - argparse owns this branch
            parser.error(f"unknown command {command}")
        document = result.as_dict(include_events=True)
        if args.output:
            _write_json(args.output, document)
        else:
            print(json.dumps(document, indent=2, ensure_ascii=True))
        return 0
    except (DiagnosticError, OSError, ValueError) as exc:
        if sys.stderr is not None:
            print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
