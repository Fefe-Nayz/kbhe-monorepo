from __future__ import annotations

import argparse
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from rgb_protocol import Command, RGBBridgeError, RGBDevice, REPORT_SIZE
from rgbctl import KNOWN_PIDS, _effect, _is_compatible_usage


class FakeTransport:
    def __init__(self):
        self.requests: list[bytes] = []
        self.frame = bytearray((index % 256 for index in range(82 * 3)))
        self.enabled = True
        self.brightness = 128
        self.effect = 0
        self.previous_effect = 0
        self.fail_command: int | None = None
        self.corrupt_echo_command: int | None = None
        self.capability_led_count = 82
        self.capability_chunk_bytes = 60
        self.capability_color_order = 0

    def exchange(self, report: bytes) -> bytes:
        self.requests.append(report)
        response = bytearray(REPORT_SIZE)
        command = report[0]
        response[0] = command
        if command == self.fail_command:
            response[1] = 3
            return bytes(response)
        if command == Command.GET_CAPABILITIES:
            response[2:11] = bytes(
                (
                    1,
                    0,
                    self.capability_led_count,
                    3,
                    self.capability_chunk_bytes,
                    7,
                    0x7F,
                    0,
                    self.capability_color_order,
                )
            )
        elif command == Command.GET_FRAME_CHUNK:
            chunk = report[2]
            data = self.frame[chunk * 60 : (chunk + 1) * 60]
            response[2] = chunk
            response[3] = len(data)
            response[4 : 4 + len(data)] = data
        elif command == Command.GET_ENABLED:
            response[2] = int(self.enabled)
        elif command == Command.SET_ENABLED:
            self.enabled = report[2] != 0
            response[2] = int(self.enabled)
        elif command == Command.GET_BRIGHTNESS:
            response[2] = self.brightness
        elif command == Command.SET_BRIGHTNESS:
            self.brightness = report[2]
            response[2] = self.brightness
        elif command == Command.GET_PIXEL:
            index = report[2]
            response[2] = index
            response[3:6] = self.frame[index * 3 : index * 3 + 3]
        elif command == Command.SET_PIXEL:
            index = report[2]
            self.frame[index * 3 : index * 3 + 3] = report[3:6]
            response[2:6] = report[2:6]
        elif command == Command.FILL:
            self.frame[:] = report[2:5] * 82
            response[2:5] = report[2:5]
        elif command == Command.CLEAR:
            self.frame[:] = bytes(len(self.frame))
        elif command == Command.GET_EFFECT:
            response[2] = self.effect
        elif command == Command.SET_EFFECT:
            if report[2] != self.effect:
                self.previous_effect = self.effect
            self.effect = report[2]
            response[2] = self.effect
        elif command == Command.RESTORE_EFFECT:
            self.effect, self.previous_effect = self.previous_effect, self.effect
            response[2] = self.effect
        if command == self.corrupt_echo_command:
            response[2] ^= 0x01
        return bytes(response)


class RGBProtocolTests(unittest.TestCase):
    def test_capability_negotiation(self) -> None:
        transport = FakeTransport()
        device = RGBDevice(transport)
        self.assertEqual(device.capabilities.led_count, 82)
        self.assertEqual(device.capabilities.frame_bytes, 246)
        self.assertEqual(transport.requests[0][0], Command.GET_CAPABILITIES)
        self.assertEqual(transport.requests[0][1], 0)

    def test_live_frame_uses_60_byte_chunks(self) -> None:
        transport = FakeTransport()
        device = RGBDevice(transport)
        frame = bytes(range(246))
        device.write_frame(frame)
        writes = [request for request in transport.requests if request[0] == Command.SET_FRAME_CHUNK]
        self.assertEqual(len(writes), 5)
        self.assertEqual([request[2] for request in writes], [0, 1, 2, 3, 4])
        self.assertEqual([request[3] for request in writes], [60, 60, 60, 60, 6])
        self.assertEqual(writes[-1][4:10], frame[-6:])
        effects = [request for request in transport.requests if request[0] == Command.SET_EFFECT]
        self.assertEqual(effects[0][2], 7)

    def test_frame_round_trip(self) -> None:
        transport = FakeTransport()
        device = RGBDevice(transport)
        self.assertEqual(device.read_frame(), bytes(transport.frame))

    def test_global_pixel_brightness_and_effect_packets(self) -> None:
        transport = FakeTransport()
        device = RGBDevice(transport)
        self.assertEqual(device.set_brightness(96), 96)
        self.assertEqual(device.get_brightness(), 96)
        device.fill(10, 20, 30)
        self.assertEqual(device.get_pixel(0), (10, 20, 30))
        device.set_pixel(81, 40, 50, 60)
        self.assertEqual(device.get_pixel(81), (40, 50, 60))
        device.enter_live_mode()
        self.assertEqual(device.get_effect(), 7)
        self.assertEqual(device.restore_effect(), 0)
        self.assertEqual(device.get_effect(), 0)
        packets = {request[0]: request for request in transport.requests[1:]}
        self.assertEqual(packets[Command.SET_BRIGHTNESS][2], 96)
        self.assertEqual(packets[Command.FILL][2:5], bytes((10, 20, 30)))
        self.assertEqual(packets[Command.SET_PIXEL][2:6], bytes((81, 40, 50, 60)))
        self.assertEqual(packets[Command.SET_EFFECT][2], 7)
        self.assertIn(Command.RESTORE_EFFECT, packets)

    def test_cli_usb_identity_and_usage_filters(self) -> None:
        self.assertEqual(KNOWN_PIDS, (0x0002, 0x0004))
        self.assertNotIn(0x0003, KNOWN_PIDS)
        self.assertTrue(_is_compatible_usage(0xFF00, 0x01))
        self.assertTrue(_is_compatible_usage(0xFFAB, 0xAB))
        self.assertTrue(_is_compatible_usage(None, None))
        self.assertFalse(_is_compatible_usage(0xFFAB, 0x01))
        self.assertFalse(_is_compatible_usage(0x0001, 0x0006))

    def test_cli_effect_parser_accepts_names_and_numeric_ids(self) -> None:
        self.assertEqual(_effect("static"), "static")
        self.assertEqual(_effect("LIVE"), "live")
        self.assertEqual(_effect("0x03"), 3)
        with self.assertRaises(argparse.ArgumentTypeError):
            _effect("256")
        with self.assertRaises(argparse.ArgumentTypeError):
            _effect("rainbow")

    def test_status_is_not_silently_ignored(self) -> None:
        transport = FakeTransport()
        device = RGBDevice(transport)
        transport.fail_command = Command.SET_BRIGHTNESS
        with self.assertRaises(RGBBridgeError):
            device.set_brightness(100)

    def test_response_echo_is_not_silently_ignored(self) -> None:
        transport = FakeTransport()
        device = RGBDevice(transport)
        transport.corrupt_echo_command = Command.SET_PIXEL
        with self.assertRaises(RGBBridgeError):
            device.set_pixel(3, 10, 20, 30)

    def test_frame_size_and_pixel_bounds(self) -> None:
        device = RGBDevice(FakeTransport())
        with self.assertRaises(ValueError):
            device.write_frame(b"too short")
        with self.assertRaises(ValueError):
            device.set_pixel(82, 1, 2, 3)

    def test_failed_frame_restores_effect_only_after_a_transition(self) -> None:
        transport = FakeTransport()
        transport.effect = 2
        transport.fail_command = Command.SET_FRAME_CHUNK
        device = RGBDevice(transport)
        with self.assertRaises(RGBBridgeError):
            device.write_frame(bytes(device.capabilities.frame_bytes))
        self.assertEqual(transport.effect, 2)
        self.assertIn(
            Command.RESTORE_EFFECT,
            [request[0] for request in transport.requests],
        )

        transport = FakeTransport()
        transport.effect = 7
        transport.fail_command = Command.SET_FRAME_CHUNK
        device = RGBDevice(transport)
        with self.assertRaises(RGBBridgeError):
            device.write_frame(bytes(device.capabilities.frame_bytes))
        self.assertEqual(transport.effect, 7)
        self.assertNotIn(
            Command.RESTORE_EFFECT,
            [request[0] for request in transport.requests],
        )

    def test_capabilities_reject_unrepresentable_chunks_and_color_order(self) -> None:
        transport = FakeTransport()
        transport.capability_led_count = 255
        transport.capability_chunk_bytes = 1
        with self.assertRaises(RGBBridgeError):
            RGBDevice(transport)

        transport = FakeTransport()
        transport.capability_color_order = 1
        with self.assertRaises(RGBBridgeError):
            RGBDevice(transport)


if __name__ == "__main__":
    unittest.main()
