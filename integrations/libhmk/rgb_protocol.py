"""KBHE/libhmk RGB bridge protocol.

The wire format is deliberately independent from either firmware's internal
data structures: one 64-byte RAW HID report, command at byte 0, status/reserved
at byte 1, and command payload from byte 2.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum, IntFlag
from typing import Protocol


REPORT_SIZE = 64
PROTOCOL_MAJOR = 1


class Command(IntEnum):
    GET_ENABLED = 0x60
    SET_ENABLED = 0x61
    GET_BRIGHTNESS = 0x62
    SET_BRIGHTNESS = 0x63
    GET_PIXEL = 0x64
    SET_PIXEL = 0x65
    GET_FRAME_CHUNK = 0x68
    SET_FRAME_CHUNK = 0x6A
    CLEAR = 0x6B
    FILL = 0x6C
    GET_EFFECT = 0x6E
    SET_EFFECT = 0x6F
    RESTORE_EFFECT = 0x76
    GET_CAPABILITIES = 0x7F


class Capability(IntFlag):
    ENABLED = 1 << 0
    BRIGHTNESS = 1 << 1
    PIXEL = 1 << 2
    FRAME_CHUNKS = 1 << 3
    FILL = 1 << 4
    LIVE_MODE = 1 << 5
    RESTORE_MODE = 1 << 6


class RGBBridgeError(RuntimeError):
    """Base error for bridge negotiation or a rejected command."""


class Transport(Protocol):
    def exchange(self, report: bytes) -> bytes:
        """Send one 64-byte report and return its 64-byte response."""


@dataclass(frozen=True)
class RGBCapabilities:
    protocol_major: int
    protocol_minor: int
    led_count: int
    bytes_per_pixel: int
    chunk_bytes: int
    live_effect_id: int
    capabilities: Capability
    color_order: int

    @property
    def frame_bytes(self) -> int:
        return self.led_count * self.bytes_per_pixel


class RGBDevice:
    """Typed controller shared by the native KBHE and optional libhmk image."""

    def __init__(self, transport: Transport):
        self._transport = transport
        self.capabilities = self._get_capabilities()

    @staticmethod
    def _packet(command: Command, payload: bytes = b"") -> bytes:
        if len(payload) > REPORT_SIZE - 2:
            raise ValueError("payload does not fit in one RAW HID report")
        report = bytearray(REPORT_SIZE)
        report[0] = command
        report[2 : 2 + len(payload)] = payload
        return bytes(report)

    def _exchange(self, command: Command, payload: bytes = b"") -> bytes:
        response = bytes(self._transport.exchange(self._packet(command, payload)))
        if len(response) != REPORT_SIZE:
            raise RGBBridgeError(
                f"short RAW HID response: expected {REPORT_SIZE}, got {len(response)}"
            )
        if response[0] != command:
            raise RGBBridgeError(
                f"unexpected response command 0x{response[0]:02x} "
                f"for 0x{int(command):02x}"
            )
        if response[1] != 0:
            raise RGBBridgeError(
                f"command 0x{int(command):02x} rejected with status {response[1]}"
            )
        return response

    def _require(self, capability: Capability) -> None:
        if not self.capabilities.capabilities & capability:
            raise RGBBridgeError(f"device does not advertise {capability.name}")

    def _get_capabilities(self) -> RGBCapabilities:
        response = self._exchange(Command.GET_CAPABILITIES)
        caps = RGBCapabilities(
            protocol_major=response[2],
            protocol_minor=response[3],
            led_count=response[4],
            bytes_per_pixel=response[5],
            chunk_bytes=response[6],
            live_effect_id=response[7],
            capabilities=Capability(int.from_bytes(response[8:10], "little")),
            color_order=response[10],
        )
        if caps.protocol_major != PROTOCOL_MAJOR:
            raise RGBBridgeError(
                f"unsupported RGB bridge major {caps.protocol_major}; "
                f"host supports {PROTOCOL_MAJOR}"
            )
        if caps.led_count == 0 or caps.bytes_per_pixel != 3:
            raise RGBBridgeError("invalid RGB geometry returned by device")
        if caps.chunk_bytes == 0 or caps.chunk_bytes > REPORT_SIZE - 4:
            raise RGBBridgeError("invalid RGB chunk size returned by device")
        chunk_count = (caps.frame_bytes + caps.chunk_bytes - 1) // caps.chunk_bytes
        if chunk_count > 256:
            raise RGBBridgeError("RGB geometry requires more than 256 chunks")
        if caps.color_order != 0:
            raise RGBBridgeError(
                f"unsupported RGB color order {caps.color_order}; expected logical RGB"
            )
        return caps

    def get_enabled(self) -> bool:
        self._require(Capability.ENABLED)
        return self._exchange(Command.GET_ENABLED)[2] != 0

    def set_enabled(self, enabled: bool) -> bool:
        self._require(Capability.ENABLED)
        expected = int(enabled)
        response = self._exchange(Command.SET_ENABLED, bytes((expected,)))
        if response[2] != expected:
            raise RGBBridgeError("device did not echo the requested enabled state")
        return response[2] != 0

    def get_brightness(self) -> int:
        self._require(Capability.BRIGHTNESS)
        return self._exchange(Command.GET_BRIGHTNESS)[2]

    def set_brightness(self, brightness: int) -> int:
        self._require(Capability.BRIGHTNESS)
        _byte(brightness, "brightness")
        response = self._exchange(Command.SET_BRIGHTNESS, bytes((brightness,)))
        if response[2] != brightness:
            raise RGBBridgeError("device did not echo the requested brightness")
        return response[2]

    def get_pixel(self, index: int) -> tuple[int, int, int]:
        self._require(Capability.PIXEL)
        self._check_index(index)
        response = self._exchange(Command.GET_PIXEL, bytes((index,)))
        if response[2] != index:
            raise RGBBridgeError("device returned the wrong RGB pixel index")
        return response[3], response[4], response[5]

    def set_pixel(self, index: int, r: int, g: int, b: int) -> None:
        self._require(Capability.PIXEL)
        self._check_index(index)
        for value, name in ((r, "red"), (g, "green"), (b, "blue")):
            _byte(value, name)
        payload = bytes((index, r, g, b))
        response = self._exchange(Command.SET_PIXEL, payload)
        if response[2:6] != payload:
            raise RGBBridgeError("device did not echo the requested RGB pixel")

    def fill(self, r: int, g: int, b: int) -> None:
        self._require(Capability.FILL)
        for value, name in ((r, "red"), (g, "green"), (b, "blue")):
            _byte(value, name)
        payload = bytes((r, g, b))
        response = self._exchange(Command.FILL, payload)
        if response[2:5] != payload:
            raise RGBBridgeError("device did not echo the requested RGB fill")

    def clear(self) -> None:
        self._require(Capability.FILL)
        self._exchange(Command.CLEAR)

    def get_effect(self) -> int:
        return self._exchange(Command.GET_EFFECT)[2]

    def set_effect(self, effect: int) -> int:
        _byte(effect, "effect")
        response = self._exchange(Command.SET_EFFECT, bytes((effect,)))
        if response[2] != effect:
            raise RGBBridgeError("device did not echo the requested RGB effect")
        return response[2]

    def enter_live_mode(self) -> None:
        self._require(Capability.LIVE_MODE)
        self.set_effect(self.capabilities.live_effect_id)

    def restore_effect(self) -> int:
        self._require(Capability.RESTORE_MODE)
        return self._exchange(Command.RESTORE_EFFECT)[2]

    def read_frame(self) -> bytes:
        self._require(Capability.FRAME_CHUNKS)
        frame = bytearray()
        chunk = 0
        while len(frame) < self.capabilities.frame_bytes:
            response = self._exchange(Command.GET_FRAME_CHUNK, bytes((chunk,)))
            if response[2] != chunk:
                raise RGBBridgeError("device returned an out-of-order RGB chunk")
            length = response[3]
            expected = min(
                self.capabilities.chunk_bytes,
                self.capabilities.frame_bytes - len(frame),
            )
            if length != expected:
                raise RGBBridgeError(
                    f"RGB chunk {chunk} has length {length}, expected {expected}"
                )
            frame.extend(response[4 : 4 + length])
            chunk += 1
        return bytes(frame)

    def write_frame(self, frame: bytes, *, enter_live: bool = True) -> None:
        self._require(Capability.FRAME_CHUNKS)
        frame = bytes(frame)
        if len(frame) != self.capabilities.frame_bytes:
            raise ValueError(
                f"frame must contain exactly {self.capabilities.frame_bytes} bytes"
            )
        restore_on_failure = False
        if enter_live:
            previous_effect = self.get_effect()
            restore_on_failure = previous_effect != self.capabilities.live_effect_id
            if restore_on_failure:
                self._require(Capability.RESTORE_MODE)
            # Re-entering live mode deliberately starts a fresh frame
            # transaction on implementations that maintain a staging bitmap.
            self.enter_live_mode()
        try:
            size = self.capabilities.chunk_bytes
            for chunk, offset in enumerate(range(0, len(frame), size)):
                data = frame[offset : offset + size]
                self._exchange(
                    Command.SET_FRAME_CHUNK, bytes((chunk, len(data))) + data
                )
        except Exception as upload_error:
            if restore_on_failure:
                try:
                    self.restore_effect()
                except Exception as rollback_error:
                    raise RGBBridgeError(
                        f"RGB frame upload failed ({upload_error}); "
                        f"effect rollback also failed ({rollback_error})"
                    ) from upload_error
            raise

    def _check_index(self, index: int) -> None:
        if not 0 <= index < self.capabilities.led_count:
            raise ValueError(
                f"LED index must be between 0 and {self.capabilities.led_count - 1}"
            )


def _byte(value: int, name: str) -> None:
    if not 0 <= value <= 255:
        raise ValueError(f"{name} must be between 0 and 255")
