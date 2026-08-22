#!/usr/bin/env python3
"""Cross-language golden vectors for KBHE release authentication."""

from __future__ import annotations

import json
import pathlib
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
PUBLIC_KEY_PATH = ROOT / "firmware" / "keys" / "firmware-ed25519-public.pem"


class ReleaseSigningVectorsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.vectors = json.loads(VECTORS_PATH.read_text(encoding="utf-8"))

    def test_public_key_matches_pem(self) -> None:
        result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-pubin",
                "-in",
                str(PUBLIC_KEY_PATH),
                "-outform",
                "DER",
            ],
            capture_output=True,
            check=True,
        )
        # Ed25519 SubjectPublicKeyInfo ends with the exact 32-byte raw key.
        self.assertGreaterEqual(len(result.stdout), 32)
        self.assertEqual(result.stdout[-32:].hex(), self.vectors["publicKeyHex"])

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
            openssl_verify(PUBLIC_KEY_PATH, firmware_bytes, firmware_signature)
            app_signature = directory / "app.sig"
            app_signature.write_bytes(bytes.fromhex(app["signatureHex"]))
            openssl_verify(PUBLIC_KEY_PATH, app_bytes, app_signature)

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
            openssl_verify(PUBLIC_KEY_PATH, artifact_bytes, artifact_signature)
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


if __name__ == "__main__":
    unittest.main()
