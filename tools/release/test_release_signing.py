#!/usr/bin/env python3
"""Cross-language golden vectors for KBHE release authentication."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import tempfile
import unittest
import zlib

from build_factory_image import (
    APP_OFFSET,
    BOOTLOADER_CODE_SIZE,
    FACTORY_IMAGE_SIZE,
    FLASH_BASE,
    TRAILER_MAGIC,
    TRAILER_OFFSET,
    VERSION_FLOOR_COMMIT_MAGIC,
    VERSION_FLOOR_MAGIC,
    VERSION_FLOOR_OFFSET,
    VERSION_FLOOR_SIZE,
    build_factory_image,
)
from build_updater_migration_package import (
    APP_BASE as MIGRATOR_APP_BASE,
    FLASH_BASE as MIGRATION_FLASH_BASE,
    MIGRATION_DESCRIPTOR_SIZE,
    MIGRATION_PACKAGE_SIZE,
    V3_TRAILER_OFFSET as MIGRATION_V3_TRAILER_OFFSET,
    build_migration_image,
    build_migration_package,
    inspect_migration_package,
    parse_migration_descriptor,
)
from sign_release_asset import (
    app_manifest,
    artifact_manifest,
    firmware_manifest,
    openssl_verify,
    parse_app_version,
    parse_version,
)


ROOT = pathlib.Path(__file__).resolve().parents[2]
VECTORS_PATH = ROOT / "firmware" / "tests" / "release_signing_vectors.json"
FIRMWARE_PUBLIC_KEY_PATH = (
    ROOT / "firmware" / "keys" / "firmware-ed25519-public.pem"
)
APP_PUBLIC_KEY_PATH = (
    ROOT / "apps" / "configurator" / "keys" / "app-ed25519-public.pem"
)


class ReleaseSigningVectorsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.vectors = json.loads(VECTORS_PATH.read_text(encoding="utf-8"))

    def test_public_keys_match_pems(self) -> None:
        for public_key_path, vector_name in (
            (FIRMWARE_PUBLIC_KEY_PATH, "firmwarePublicKeyHex"),
            (APP_PUBLIC_KEY_PATH, "appPublicKeyHex"),
        ):
            with self.subTest(public_key_path=public_key_path):
                result = subprocess.run(
                    [
                        "openssl",
                        "pkey",
                        "-pubin",
                        "-in",
                        str(public_key_path),
                        "-outform",
                        "DER",
                    ],
                    capture_output=True,
                    check=True,
                )
                # Ed25519 SPKI ends with the exact 32-byte raw key.
                self.assertGreaterEqual(len(result.stdout), 32)
                self.assertEqual(
                    result.stdout[-32:].hex(), self.vectors[vector_name]
                )

    def test_documented_firmware_public_key_fingerprint_matches_pem(self) -> None:
        result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-pubin",
                "-in",
                str(FIRMWARE_PUBLIC_KEY_PATH),
                "-outform",
                "DER",
            ],
            capture_output=True,
            check=True,
        )
        derived_fingerprint = hashlib.sha256(result.stdout).hexdigest()
        installation_guide = (
            ROOT / "docs" / "firmware" / "INSTALLATION.md"
        ).read_text(encoding="utf-8")
        documented = re.search(
            r"expected SHA-256 fingerprint of the public-key DER is:\s*"
            r"```text\s*([0-9a-fA-F]{64})\s*```",
            installation_guide,
        )
        self.assertIsNotNone(documented)
        self.assertEqual(documented.group(1).lower(), derived_fingerprint)

    def test_release_version_source_is_the_shared_header(self) -> None:
        header_path = ROOT / "firmware" / "Core" / "Inc" / "firmware_version.h"
        header = header_path.read_text(encoding="utf-8")
        for component in ("MAJOR", "MINOR", "PATCH"):
            match = re.search(
                rf"^#define\s+FIRMWARE_VERSION_{component}\s+(\d+)u\s*$",
                header,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, component)
            self.assertLessEqual(int(match.group(1)), 255)

        settings = (ROOT / "firmware" / "Core" / "Src" / "settings.c").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(settings, r"^#define\s+FIRMWARE_VERSION_",)
        release_script = (ROOT / "tools" / "release" / "run-release.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("firmware/Core/Inc/firmware_version.h", release_script)
        self.assertNotIn("firmware/Core/Src/settings.c", release_script)

    def test_python_manifests_and_signatures_match_golden(self) -> None:
        firmware = self.vectors["firmware"]
        firmware_data = bytes.fromhex(firmware["dataHex"])
        firmware_bytes = firmware_manifest(firmware_data, firmware["version"])
        self.assertEqual(firmware_bytes.hex(), firmware["manifestHex"])

        app = self.vectors["app"]
        app_data = bytes.fromhex(app["dataHex"])
        app_bytes = app_manifest(
            app_data, app["version"], app["platform"], app["arch"], app["role"]
        )
        self.assertEqual(app_bytes.hex(), app["manifestHex"])

        with tempfile.TemporaryDirectory(prefix="kbhe-signing-test-") as raw_dir:
            directory = pathlib.Path(raw_dir)
            firmware_signature = directory / "firmware.sig"
            firmware_signature.write_bytes(bytes.fromhex(firmware["signatureHex"]))
            openssl_verify(
                FIRMWARE_PUBLIC_KEY_PATH, firmware_bytes, firmware_signature
            )
            app_signature = directory / "app.sig"
            app_signature.write_bytes(bytes.fromhex(app["signatureHex"]))
            openssl_verify(APP_PUBLIC_KEY_PATH, app_bytes, app_signature)

            artifact = self.vectors["artifact"]
            artifact_data = bytes.fromhex(artifact["dataHex"])
            artifact_bytes = artifact_manifest(
                artifact_data,
                artifact["version"],
                artifact["target"],
                artifact["role"],
            )
            self.assertEqual(artifact_bytes.hex(), artifact["manifestHex"])
            artifact_signature = directory / "artifact.sig"
            artifact_signature.write_bytes(bytes.fromhex(artifact["signatureHex"]))
            openssl_verify(
                FIRMWARE_PUBLIC_KEY_PATH, artifact_bytes, artifact_signature
            )
            self.assertNotEqual(
                artifact_bytes,
                artifact_manifest(
                    artifact_data,
                    "1.2.4",
                    artifact["target"],
                    artifact["role"],
                ),
            )
            self.assertNotEqual(
                artifact_bytes,
                artifact_manifest(
                    artifact_data,
                    artifact["version"],
                    "different-hardware",
                    artifact["role"],
                ),
            )

    def test_release_versions_are_canonical(self) -> None:
        self.assertEqual(parse_version("firmware-v1.2.3"), (1, 2, 3))
        self.assertEqual(parse_app_version("app-v1.2.3"), "1.2.3")
        for invalid in (
            "01.2.3",
            "+1.2.3",
            "1.02.3",
            "1.2.03",
            "1.2.3+build",
            "firmware-vv1.2.3",
            "app-vv1.2.3",
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    parse_version(invalid)
                with self.assertRaises(ValueError):
                    parse_app_version(invalid)

    def test_factory_layout_contains_bootable_signed_trailer(self) -> None:
        firmware = self.vectors["firmware"]
        app = bytes.fromhex(firmware["dataHex"])
        signature = bytes.fromhex(firmware["signatureHex"])
        bootloader = struct.pack("<II", 0x2003FF00, FLASH_BASE + 9) + bytes(56)
        factory = build_factory_image(bootloader, app, signature, firmware["version"])

        self.assertEqual(len(factory), FACTORY_IMAGE_SIZE)
        self.assertEqual(factory[: len(bootloader)], bootloader)
        floor = factory[VERSION_FLOOR_OFFSET : VERSION_FLOOR_OFFSET + 16]
        floor_magic, floor_major, floor_minor, floor_patch, floor_reserved = (
            struct.unpack_from("<I4B", floor)
        )
        self.assertEqual(floor_magic, VERSION_FLOOR_MAGIC)
        self.assertEqual(
            (floor_major, floor_minor, floor_patch),
            parse_version(firmware["version"]),
        )
        self.assertEqual(floor_reserved, 0)
        self.assertEqual(struct.unpack_from("<I", floor, 8)[0], zlib.crc32(floor[:8]))
        self.assertEqual(
            struct.unpack_from("<I", floor, 12)[0],
            VERSION_FLOOR_COMMIT_MAGIC,
        )
        self.assertEqual(
            factory[VERSION_FLOOR_OFFSET + 16 : APP_OFFSET],
            b"\xFF" * (VERSION_FLOOR_SIZE - 16),
        )
        self.assertEqual(factory[APP_OFFSET : APP_OFFSET + len(app)], app)
        trailer = factory[TRAILER_OFFSET : TRAILER_OFFSET + 84]
        magic, size, image_crc = struct.unpack_from("<III", trailer)
        self.assertEqual(magic, TRAILER_MAGIC)
        self.assertEqual(size, len(app))
        self.assertEqual(image_crc, zlib.crc32(app))
        self.assertEqual(trailer[16:80], signature)
        self.assertEqual(struct.unpack_from("<I", trailer, 80)[0], zlib.crc32(trailer[:80]))

        oversized_bootloader = bootloader + bytes(
            BOOTLOADER_CODE_SIZE + 1 - len(bootloader)
        )
        with self.assertRaisesRegex(ValueError, "49152"):
            build_factory_image(
                oversized_bootloader, app, signature, firmware["version"]
            )
        shared_ram_stack = (
            struct.pack("<II", 0x20040000, FLASH_BASE + 9) + bytes(56)
        )
        with self.assertRaisesRegex(ValueError, "bootloader stack pointer"):
            build_factory_image(
                shared_ram_stack, app, signature, firmware["version"]
            )

    def test_v2_capsule_detached_signature_is_the_inner_v3_signature(self) -> None:
        firmware = self.vectors["firmware"]
        version = firmware["version"]
        packed_version = tuple(int(part) for part in version.split("."))
        version_word = (
            (packed_version[0] << 16)
            | (packed_version[1] << 8)
            | packed_version[2]
        )
        migrator = b"".join(
            (
                struct.pack("<II", 0x2003FF00, MIGRATOR_APP_BASE + 9),
                bytes(24),
                struct.pack(
                    "<III", 0x4B465756, version_word, version_word ^ 0xFFFFFFFF
                ),
                bytes(20),
            )
        )
        bootloader = (
            struct.pack("<II", 0x2003FF00, MIGRATION_FLASH_BASE + 9) + bytes(56)
        )
        image = build_migration_image(migrator, bootloader)
        descriptor = parse_migration_descriptor(image)
        self.assertEqual(descriptor["image_size"], len(image))
        self.assertEqual(descriptor["bootloader_size"], len(bootloader))
        self.assertGreaterEqual(len(image), len(migrator) + len(bootloader) + MIGRATION_DESCRIPTOR_SIZE)

        # The golden signature does not sign this synthetic image; package
        # construction is intentionally separate from cryptographic verification.
        signature = bytes.fromhex(firmware["signatureHex"])
        package = build_migration_package(image, signature, version)
        inspected = inspect_migration_package(package, signature, version)
        self.assertEqual(inspected, descriptor)
        self.assertEqual(len(package), MIGRATION_PACKAGE_SIZE)
        self.assertEqual(package[: len(image)], image)
        self.assertEqual(
            package[len(image) : MIGRATION_V3_TRAILER_OFFSET],
            b"\xFF" * (MIGRATION_V3_TRAILER_OFFSET - len(image)),
        )
        trailer = package[MIGRATION_V3_TRAILER_OFFSET:]
        magic, image_size, image_crc = struct.unpack_from("<III", trailer)
        self.assertEqual(magic, TRAILER_MAGIC)
        self.assertEqual(image_size, len(image))
        self.assertEqual(image_crc, zlib.crc32(image))
        self.assertEqual(trailer[16:80], signature)
        self.assertEqual(struct.unpack_from("<I", trailer, 80)[0], zlib.crc32(trailer[:80]))

        tampered = bytearray(image)
        tampered[descriptor["bootloader_offset"] + 8] ^= 1
        with self.assertRaisesRegex(ValueError, "bootloader CRC"):
            parse_migration_descriptor(bytes(tampered))

        unknown_flags = bytearray(image)
        descriptor_offset = len(unknown_flags) - MIGRATION_DESCRIPTOR_SIZE
        flags_offset = descriptor_offset + 16
        struct.pack_into(
            "<I",
            unknown_flags,
            flags_offset,
            struct.unpack_from("<I", unknown_flags, flags_offset)[0] | (1 << 31),
        )
        struct.pack_into(
            "<I",
            unknown_flags,
            len(unknown_flags) - 4,
            zlib.crc32(unknown_flags[descriptor_offset : len(unknown_flags) - 4]),
        )
        with self.assertRaisesRegex(ValueError, "exact supported recovery contract"):
            parse_migration_descriptor(bytes(unknown_flags))

        for label, offset in (
            ("signed image", 20),
            ("erased padding", len(image)),
            ("v3 trailer", MIGRATION_V3_TRAILER_OFFSET),
            ("embedded signature", MIGRATION_V3_TRAILER_OFFSET + 16),
            ("trailer CRC", len(package) - 1),
        ):
            with self.subTest(label=label):
                corrupted = bytearray(package)
                corrupted[offset] ^= 1
                with self.assertRaises(ValueError):
                    inspect_migration_package(bytes(corrupted), signature, version)

        with self.assertRaisesRegex(ValueError, "detached signature"):
            inspect_migration_package(package, bytes(64), version)

    def test_firmware_workflow_keeps_migration_release_draft_for_hil(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "firmware.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("build_updater_migration_package.py inspect", workflow)
        self.assertIn(
            "kbhe-updater-v2-to-v3.bin.sig deliberately authenticates the exact",
            workflow,
        )
        self.assertIn("! -name 'kbhe-updater-v2-to-v3.bin'", workflow)
        self.assertIn("draft: true", workflow)
        self.assertNotIn("--draft=false", workflow)


if __name__ == "__main__":
    unittest.main()
