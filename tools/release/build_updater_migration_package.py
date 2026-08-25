#!/usr/bin/env python3
"""Build the signed updater v2 migration and v3 refresh artifacts.

The package is deliberately not a normal application image.  Protocol v2
transfers the complete package and writes its own validity trailer in sector 6.
The future migrator boots from sectors 4-5, installs updater v3 while BOOT_ADD0
temporarily points at the migrator, then hands the already signed v3 trailer to
the new updater.  A normal kbhe-app.bin must never be sent through protocol v2:
its banked storage uses sector 6 and would destroy the v2 validity trailer.

Signing is a two-step operation because the v3 trailer contains the detached
signature of the inner migrator image:

  1. prepare: migrator + embedded bootloader + signed descriptor -> v3 refresh
  2. sign the inner image with sign_release_asset.py firmware
  3. assemble: inner image + detached signature -> v2 migration package
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import sys
import zlib

from sign_release_asset import firmware_manifest, openssl_verify, parse_version


FLASH_BASE = 0x08000000
BOOTLOADER_MAX_SIZE = 0x0000C000
APP_BASE = 0x08010000
V3_APP_MAX_IMAGE_SIZE = 0x0002FF00
MIGRATOR_EXECUTABLE_MAX_SIZE = 0x00010000
V2_APP_MAX_IMAGE_SIZE = 0x0004FF00
V3_TRAILER_OFFSET = V3_APP_MAX_IMAGE_SIZE
V3_TRAILER_MAGIC = 0x55445452
V3_TRAILER_SIZE = 84
MIGRATION_PACKAGE_SIZE = V3_TRAILER_OFFSET + V3_TRAILER_SIZE

MIGRATION_DESCRIPTOR_MAGIC = b"KBHEMIG3"
MIGRATION_DESCRIPTOR_SIZE = 128
MIGRATION_DESCRIPTOR_SCHEMA = 2
MIGRATION_SOURCE_PROTOCOL = 2
MIGRATION_TARGET_PROTOCOL = 3
MIGRATION_TARGET_ID = b"KBHE75HEF723VET6"
MIGRATION_FLAG_BOOTADDR_RESUMABLE = 1 << 0
MIGRATION_FLAG_V3_TRAILER_PRESEEDED = 1 << 1
MIGRATION_FLAG_V3_REFRESH_ALLOWED = 1 << 2
MIGRATION_FLAGS = (
    MIGRATION_FLAG_BOOTADDR_RESUMABLE
    | MIGRATION_FLAG_V3_TRAILER_PRESEEDED
    | MIGRATION_FLAG_V3_REFRESH_ALLOWED
)
BOOTLOADER_VERSION_RECORD_MAGIC = 0x4B424C56
RAM_BASE = 0x20000000
RAM_END = 0x2003FF00


def align_up(value: int, alignment: int = 4) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def validate_vector(image: bytes, base: int, maximum_size: int, label: str) -> None:
    if len(image) < 8 or len(image) > maximum_size:
        raise ValueError(
            f"{label} has {len(image)} bytes; expected 8..={maximum_size}"
        )
    initial_sp, reset_handler = struct.unpack_from("<II", image)
    reset_address = reset_handler & ~1
    if (
        initial_sp <= RAM_BASE
        or initial_sp > RAM_END
        or initial_sp & 7
        or not reset_handler & 1
        or reset_address < base
        or reset_address >= base + len(image)
    ):
        raise ValueError(f"{label} vector table is not valid for 0x{base:08X}")


def bootloader_version_values(image: bytes) -> set[int]:
    marker = struct.pack("<I", BOOTLOADER_VERSION_RECORD_MAGIC)
    versions: set[int] = set()
    offset = 0
    while True:
        offset = image.find(marker, offset)
        if offset < 0:
            break
        if offset + 12 <= len(image):
            _magic, packed, inverse = struct.unpack_from("<III", image, offset)
            if (packed ^ inverse) == 0xFFFFFFFF and packed <= 0x00FFFFFF:
                versions.add(packed)
        offset += 1
    return versions


def parse_bootloader_version(image: bytes) -> tuple[int, int, int]:
    versions = bootloader_version_values(image)
    if len(versions) != 1:
        found = ", ".join(f"0x{value:06x}" for value in sorted(versions)) or "none"
        raise ValueError(
            f"bootloader version metadata must contain one unambiguous KBLV record; found {found}"
        )
    packed = next(iter(versions))
    return ((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF)


def validate_bootloader_lineage(
    current: bytes, previous: bytes
) -> tuple[tuple[int, int, int], tuple[int, int, int] | None]:
    """Reject resident updater downgrades and unversioned binary replacement."""
    current_version = parse_bootloader_version(current)
    previous_values = bootloader_version_values(previous)
    if not previous_values:
        return current_version, None
    if len(previous_values) != 1:
        found = ", ".join(
            f"0x{value:06x}" for value in sorted(previous_values)
        )
        raise ValueError(
            f"previous bootloader has ambiguous KBLV metadata: {found}"
        )
    previous_packed = next(iter(previous_values))
    previous_version = (
        (previous_packed >> 16) & 0xFF,
        (previous_packed >> 8) & 0xFF,
        previous_packed & 0xFF,
    )
    if current_version < previous_version:
        raise ValueError(
            f"bootloader KBLV decreased from {previous_version} to {current_version}"
        )
    if (
        current_version == previous_version
        and hashlib.sha512(current).digest()
        != hashlib.sha512(previous).digest()
    ):
        raise ValueError("bootloader binary changed without a KBLV bump")
    return current_version, previous_version


def parse_migration_descriptor(
    image: bytes,
) -> dict[str, int | bytes | tuple[int, int, int]]:
    if len(image) < MIGRATION_DESCRIPTOR_SIZE:
        raise ValueError("migration image is shorter than its descriptor")
    descriptor = image[-MIGRATION_DESCRIPTOR_SIZE:]
    (
        magic,
        schema,
        descriptor_size,
        source_protocol,
        target_protocol,
        flags,
        bootloader_offset,
        bootloader_size,
        bootloader_crc32,
        image_size,
        target_id,
        bootloader_sha512,
        bootloader_version_major,
        bootloader_version_minor,
        bootloader_version_patch,
        reserved0,
        reserved,
        descriptor_crc32,
    ) = struct.unpack("<8sHHHHIIIII16s64s4B4sI", descriptor)
    if magic != MIGRATION_DESCRIPTOR_MAGIC:
        raise ValueError("migration descriptor magic is missing")
    if schema != MIGRATION_DESCRIPTOR_SCHEMA or descriptor_size != len(descriptor):
        raise ValueError("unsupported migration descriptor schema")
    if (source_protocol, target_protocol) != (
        MIGRATION_SOURCE_PROTOCOL,
        MIGRATION_TARGET_PROTOCOL,
    ):
        raise ValueError("migration descriptor is not for updater v2-to-v3")
    if flags != MIGRATION_FLAGS:
        raise ValueError(
            "migration descriptor flags are not the exact supported recovery contract"
        )
    if image_size != len(image):
        raise ValueError("migration descriptor image size is inconsistent")
    if target_id != MIGRATION_TARGET_ID:
        raise ValueError("migration descriptor targets different hardware")
    bootloader_version = (
        bootloader_version_major,
        bootloader_version_minor,
        bootloader_version_patch,
    )
    if bootloader_version == (0, 0, 0):
        raise ValueError("migration descriptor bootloader version is zero")
    if reserved0 != 0 or reserved != bytes(len(reserved)):
        raise ValueError("migration descriptor reserved bytes are non-zero")
    if descriptor_crc32 != zlib.crc32(descriptor[:-4]):
        raise ValueError("migration descriptor CRC is invalid")

    bootloader_end = bootloader_offset + bootloader_size
    descriptor_offset = len(image) - MIGRATION_DESCRIPTOR_SIZE
    if (
        bootloader_offset < 8
        or bootloader_offset > MIGRATOR_EXECUTABLE_MAX_SIZE
        or bootloader_offset & 3
        or bootloader_size == 0
        or bootloader_size > BOOTLOADER_MAX_SIZE
        or bootloader_end > descriptor_offset
    ):
        raise ValueError("embedded bootloader range is invalid")
    bootloader = image[bootloader_offset:bootloader_end]
    if zlib.crc32(bootloader) != bootloader_crc32:
        raise ValueError("embedded bootloader CRC does not match")
    if hashlib.sha512(bootloader).digest() != bootloader_sha512:
        raise ValueError("embedded bootloader SHA-512 does not match")
    if parse_bootloader_version(bootloader) != bootloader_version:
        raise ValueError(
            "embedded bootloader KBLV version does not match the signed descriptor"
        )
    validate_vector(bootloader, FLASH_BASE, BOOTLOADER_MAX_SIZE, "bootloader")
    validate_vector(image, APP_BASE, V3_APP_MAX_IMAGE_SIZE, "migrator")
    return {
        "image_size": image_size,
        "bootloader_offset": bootloader_offset,
        "bootloader_size": bootloader_size,
        "bootloader_crc32": bootloader_crc32,
        "bootloader_sha512": bootloader_sha512,
        "bootloader_version": bootloader_version,
    }


def build_migration_image(migrator: bytes, bootloader: bytes) -> bytes:
    validate_vector(
        migrator, APP_BASE, MIGRATOR_EXECUTABLE_MAX_SIZE, "migrator"
    )
    validate_vector(bootloader, FLASH_BASE, BOOTLOADER_MAX_SIZE, "bootloader")
    bootloader_version = parse_bootloader_version(bootloader)

    bootloader_offset = align_up(len(migrator))
    image_size = bootloader_offset + len(bootloader) + MIGRATION_DESCRIPTOR_SIZE
    if image_size > V3_APP_MAX_IMAGE_SIZE:
        raise ValueError(
            f"migration image needs {image_size} bytes; v3 slot allows "
            f"{V3_APP_MAX_IMAGE_SIZE}"
        )

    image = bytearray(b"\xFF" * image_size)
    image[: len(migrator)] = migrator
    image[bootloader_offset : bootloader_offset + len(bootloader)] = bootloader
    descriptor_without_crc = struct.pack(
        "<8sHHHHIIIII16s64s4B4s",
        MIGRATION_DESCRIPTOR_MAGIC,
        MIGRATION_DESCRIPTOR_SCHEMA,
        MIGRATION_DESCRIPTOR_SIZE,
        MIGRATION_SOURCE_PROTOCOL,
        MIGRATION_TARGET_PROTOCOL,
        MIGRATION_FLAGS,
        bootloader_offset,
        len(bootloader),
        zlib.crc32(bootloader),
        image_size,
        MIGRATION_TARGET_ID,
        hashlib.sha512(bootloader).digest(),
        *bootloader_version,
        0,
        bytes(4),
    )
    if len(descriptor_without_crc) != MIGRATION_DESCRIPTOR_SIZE - 4:
        raise AssertionError("migration descriptor layout changed")
    descriptor = descriptor_without_crc + struct.pack(
        "<I", zlib.crc32(descriptor_without_crc)
    )
    image[-MIGRATION_DESCRIPTOR_SIZE:] = descriptor
    result = bytes(image)
    parse_migration_descriptor(result)
    return result


def build_migration_package(
    image: bytes, signature: bytes, version_raw: str
) -> bytes:
    parse_migration_descriptor(image)
    if len(signature) != 64:
        raise ValueError(
            f"migration signature has {len(signature)} bytes; expected 64"
        )
    major, minor, patch = parse_version(version_raw)
    # Validate the embedded KFWV record and construct the exact manifest whose
    # signature is checked by both the host and updater v3.
    firmware_manifest(image, version_raw)

    trailer_without_crc = struct.pack(
        "<III4B64s",
        V3_TRAILER_MAGIC,
        len(image),
        zlib.crc32(image),
        major,
        minor,
        patch,
        0,
        signature,
    )
    trailer = trailer_without_crc + struct.pack(
        "<I", zlib.crc32(trailer_without_crc)
    )
    if len(trailer) != V3_TRAILER_SIZE:
        raise AssertionError("updater v3 trailer layout changed")

    package = bytearray(b"\xFF" * MIGRATION_PACKAGE_SIZE)
    package[: len(image)] = image
    package[V3_TRAILER_OFFSET : V3_TRAILER_OFFSET + len(trailer)] = trailer
    if len(package) > V2_APP_MAX_IMAGE_SIZE:
        raise AssertionError("migration package no longer fits updater v2")
    result = bytes(package)
    inspect_migration_package(result, signature, version_raw)
    return result


def inspect_migration_package(
    package: bytes, detached_signature: bytes, version_raw: str
) -> dict[str, int | bytes | tuple[int, int, int]]:
    """Fail closed unless *package* is the one canonical v2-to-v3 capsule."""
    if len(package) != MIGRATION_PACKAGE_SIZE:
        raise ValueError(
            f"migration package has {len(package)} bytes; expected "
            f"{MIGRATION_PACKAGE_SIZE}"
        )
    if len(detached_signature) != 64:
        raise ValueError(
            f"migration signature has {len(detached_signature)} bytes; expected 64"
        )

    trailer = package[V3_TRAILER_OFFSET:]
    (
        magic,
        image_size,
        image_crc32,
        major,
        minor,
        patch,
        reserved,
        embedded_signature,
        trailer_crc32,
    ) = struct.unpack("<III4B64sI", trailer)
    if magic != V3_TRAILER_MAGIC:
        raise ValueError("migration package v3 trailer magic is invalid")
    if trailer_crc32 != zlib.crc32(trailer[:-4]):
        raise ValueError("migration package v3 trailer CRC is invalid")
    if reserved != 0:
        raise ValueError("migration package v3 trailer reserved byte is non-zero")
    if (major, minor, patch) != parse_version(version_raw):
        raise ValueError("migration package version does not match the release")
    if embedded_signature != detached_signature:
        raise ValueError(
            "migration package trailer signature does not match the detached signature"
        )
    if image_size < MIGRATION_DESCRIPTOR_SIZE or image_size > V3_TRAILER_OFFSET:
        raise ValueError("migration package signed image size is invalid")

    image = package[:image_size]
    if zlib.crc32(image) != image_crc32:
        raise ValueError("migration package signed image CRC is invalid")
    if any(byte != 0xFF for byte in package[image_size:V3_TRAILER_OFFSET]):
        raise ValueError("migration package padding must remain erased")
    descriptor = parse_migration_descriptor(image)
    # Also validates the single embedded KFWV record against version_raw.
    firmware_manifest(image, version_raw)
    return descriptor


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare = subparsers.add_parser("prepare")
    prepare.add_argument("--migrator", required=True, type=pathlib.Path)
    prepare.add_argument("--bootloader", required=True, type=pathlib.Path)
    prepare.add_argument("--output", required=True, type=pathlib.Path)

    assemble = subparsers.add_parser("assemble")
    assemble.add_argument("--image", required=True, type=pathlib.Path)
    assemble.add_argument("--signature", required=True, type=pathlib.Path)
    assemble.add_argument("--version", required=True)
    assemble.add_argument("--verify-public", required=True, type=pathlib.Path)
    assemble.add_argument("--output", required=True, type=pathlib.Path)

    inspect_image = subparsers.add_parser("inspect-image")
    inspect_image.add_argument("--input", required=True, type=pathlib.Path)
    inspect_image.add_argument("--signature", required=True, type=pathlib.Path)
    inspect_image.add_argument("--version", required=True)
    inspect_image.add_argument("--verify-public", required=True, type=pathlib.Path)

    inspect_lineage = subparsers.add_parser("inspect-lineage")
    inspect_lineage.add_argument("--current", required=True, type=pathlib.Path)
    inspect_lineage.add_argument("--previous", required=True, type=pathlib.Path)

    inspect = subparsers.add_parser("inspect")
    inspect.add_argument("--input", required=True, type=pathlib.Path)
    inspect.add_argument("--signature", required=True, type=pathlib.Path)
    inspect.add_argument("--version", required=True)
    inspect.add_argument("--verify-public", required=True, type=pathlib.Path)
    args = parser.parse_args()

    if args.command == "prepare":
        image = build_migration_image(
            args.migrator.read_bytes(), args.bootloader.read_bytes()
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(image)
        print(
            f"prepared {args.output} ({len(image)} bytes; sign this exact inner image)"
        )
        return 0

    if args.command == "assemble":
        image = args.image.read_bytes()
        signature = args.signature.read_bytes()
        manifest = firmware_manifest(image, args.version)
        openssl_verify(args.verify_public, manifest, args.signature)
        package = build_migration_package(image, signature, args.version)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(package)
        print(
            f"assembled {args.output} ({len(package)} bytes; signed image={len(image)}; "
            f"v3 trailer=0x{V3_TRAILER_OFFSET:05X})"
        )
        return 0

    if args.command == "inspect-image":
        image = args.input.read_bytes()
        signature = args.signature.read_bytes()
        if len(signature) != 64:
            raise ValueError(
                f"migration signature has {len(signature)} bytes; expected 64"
            )
        descriptor = parse_migration_descriptor(image)
        manifest = firmware_manifest(image, args.version)
        openssl_verify(args.verify_public, manifest, args.signature)
        print(
            f"verified {args.input} ({len(image)} bytes; "
            f"bootloader={descriptor['bootloader_size']} bytes; "
            f"bootloader-version={'.'.join(map(str, descriptor['bootloader_version']))})"
        )
        return 0

    if args.command == "inspect-lineage":
        current_version, previous_version = validate_bootloader_lineage(
            args.current.read_bytes(), args.previous.read_bytes()
        )
        previous_label = (
            ".".join(map(str, previous_version))
            if previous_version is not None
            else "legacy-unversioned"
        )
        print(
            f"verified bootloader lineage {previous_label} -> "
            f"{'.'.join(map(str, current_version))}"
        )
        return 0

    package = args.input.read_bytes()
    signature = args.signature.read_bytes()
    descriptor = inspect_migration_package(package, signature, args.version)
    image_size = int(descriptor["image_size"])
    manifest = firmware_manifest(package[:image_size], args.version)
    openssl_verify(args.verify_public, manifest, args.signature)
    print(
        f"verified {args.input} ({len(package)} bytes; signed image={image_size}; "
        f"bootloader={descriptor['bootloader_size']} bytes; "
        f"bootloader-version={'.'.join(map(str, descriptor['bootloader_version']))})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
