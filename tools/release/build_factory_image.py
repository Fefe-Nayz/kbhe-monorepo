#!/usr/bin/env python3
"""Assemble a bootable, signed KBHE v3 factory image for ST-Link/DFU."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys
import zlib

from sign_release_asset import (
    SIGNATURE_SIZE,
    firmware_manifest,
    openssl_verify,
    parse_version,
)


FLASH_BASE = 0x08000000
BOOTLOADER_CODE_SIZE = 0x0000C000
VERSION_FLOOR_OFFSET = 0x0000C000
VERSION_FLOOR_SIZE = 0x00004000
APP_OFFSET = 0x00010000
APP_MAX_SIZE = 0x0002FF00
TRAILER_OFFSET = 0x0003FF00
FACTORY_IMAGE_SIZE = 0x00040000
TRAILER_MAGIC = 0x55445452
VERSION_FLOOR_MAGIC = 0x4B46564C
VERSION_FLOOR_COMMIT_MAGIC = 0x434F4D54
RAM_BASE = 0x20000000
RAM_END = 0x2003FF00
BOOTLOADER_RAM_END = RAM_END


def _validate_vectors(image: bytes, image_base: int, ram_end: int, label: str) -> None:
    if len(image) < 8:
        raise ValueError(f"{label} image is too short to contain a vector table")
    stack_pointer, reset_handler = struct.unpack_from("<II", image)
    reset_address = reset_handler & ~1
    if stack_pointer & 7 or not (RAM_BASE <= stack_pointer <= ram_end):
        raise ValueError(f"invalid {label} stack pointer 0x{stack_pointer:08X}")
    if reset_handler & 1 == 0:
        raise ValueError(f"{label} reset handler is not a Thumb address")
    if not (image_base <= reset_address < image_base + len(image)):
        raise ValueError(f"{label} reset handler 0x{reset_handler:08X} is outside the image")


def build_factory_image(
    bootloader: bytes, app: bytes, signature: bytes, version_raw: str
) -> bytes:
    if not bootloader or len(bootloader) > BOOTLOADER_CODE_SIZE:
        raise ValueError("bootloader must contain 1..49152 bytes")
    if not app or len(app) > APP_MAX_SIZE:
        raise ValueError(f"application must contain 1..{APP_MAX_SIZE} bytes")
    if len(signature) != SIGNATURE_SIZE:
        raise ValueError(f"firmware signature must contain {SIGNATURE_SIZE} bytes")
    _validate_vectors(bootloader, FLASH_BASE, BOOTLOADER_RAM_END, "bootloader")
    _validate_vectors(app, FLASH_BASE + APP_OFFSET, RAM_END, "application")

    major, minor, patch = parse_version(version_raw)
    floor_prefix = struct.pack(
        "<I4B", VERSION_FLOOR_MAGIC, major, minor, patch, 0
    )
    floor_entry = floor_prefix + struct.pack(
        "<II", zlib.crc32(floor_prefix), VERSION_FLOOR_COMMIT_MAGIC
    )
    trailer_without_crc = struct.pack(
        "<III4B64s",
        TRAILER_MAGIC,
        len(app),
        zlib.crc32(app),
        major,
        minor,
        patch,
        0,
        signature,
    )
    trailer = trailer_without_crc + struct.pack("<I", zlib.crc32(trailer_without_crc))

    factory = bytearray(b"\xFF" * FACTORY_IMAGE_SIZE)
    factory[: len(bootloader)] = bootloader
    factory[
        VERSION_FLOOR_OFFSET : VERSION_FLOOR_OFFSET + len(floor_entry)
    ] = floor_entry
    factory[APP_OFFSET : APP_OFFSET + len(app)] = app
    factory[TRAILER_OFFSET : TRAILER_OFFSET + len(trailer)] = trailer
    return bytes(factory)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bootloader", required=True, type=pathlib.Path)
    parser.add_argument("--app", required=True, type=pathlib.Path)
    parser.add_argument("--signature", required=True, type=pathlib.Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--verify-public", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    args = parser.parse_args()

    bootloader = args.bootloader.read_bytes()
    app = args.app.read_bytes()
    signature = args.signature.read_bytes()

    # This also rejects a tag that does not match the KFWV record in the app.
    manifest = firmware_manifest(app, args.version)
    openssl_verify(args.verify_public, manifest, args.signature)

    factory = build_factory_image(bootloader, app, signature, args.version)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(factory)
    if args.manifest:
        release_manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if not isinstance(release_manifest, dict):
            raise ValueError("release manifest must contain a JSON object")
        release_manifest["factoryBinary"] = args.output.name
        args.manifest.write_text(
            json.dumps(release_manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(
        f"assembled {args.output} ({len(factory)} bytes, app={len(app)} bytes, "
        f"trailer=0x{TRAILER_OFFSET:05X})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
