#!/usr/bin/env python3
"""Create detached Ed25519 signatures for KBHE release assets.

The signature covers a small canonical manifest instead of the raw file so
that the asset kind, byte length, firmware metadata and SHA-512 digest are all
bound to the release key. The exact layouts are mirrored in the bootloader and
the configurator's Rust verifier.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import zlib


FIRMWARE_CONTEXT = b"KBHEFW3\0"
APP_CONTEXT = b"KBHEAPP2"
ARTIFACT_CONTEXT = b"KBHEART2"
SIGNATURE_SIZE = 64
FIRMWARE_VERSION_RECORD_MAGIC = 0x4B465756


def parse_version(raw: str) -> tuple[int, int, int]:
    if raw.startswith("firmware-v"):
        canonical = raw.removeprefix("firmware-v")
    elif raw.startswith("v"):
        canonical = raw.removeprefix("v")
    else:
        canonical = raw
    if re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", canonical) is None:
        raise ValueError("firmware version must be canonical MAJOR.MINOR.PATCH")
    parts = canonical.split(".")
    version = tuple(int(part, 10) for part in parts)
    if any(part < 0 or part > 255 for part in version):
        raise ValueError("firmware version components must fit in one byte")
    return version  # type: ignore[return-value]


def firmware_manifest(data: bytes, version_raw: str) -> bytes:
    major, minor, patch = parse_version(version_raw)
    expected_version = (major << 16) | (minor << 8) | patch
    discovered_versions: set[int] = set()
    marker = struct.pack("<I", FIRMWARE_VERSION_RECORD_MAGIC)
    offset = 0
    while True:
        offset = data.find(marker, offset)
        if offset < 0:
            break
        if offset + 12 <= len(data):
            _magic, packed, inverse = struct.unpack_from("<III", data, offset)
            if (packed ^ inverse) == 0xFFFFFFFF:
                discovered_versions.add(packed)
        offset += 1
    if discovered_versions != {expected_version}:
        found = ", ".join(f"0x{value:06x}" for value in sorted(discovered_versions)) or "none"
        raise ValueError(
            f"firmware metadata ({found}) does not match release version 0x{expected_version:06x}"
        )
    return b"".join(
        (
            FIRMWARE_CONTEXT,
            struct.pack("<II4B", len(data), zlib.crc32(data), major, minor, patch, 0),
            hashlib.sha512(data).digest(),
        )
    )


def parse_app_version(raw: str) -> str:
    if raw.startswith("app-v"):
        version = raw.removeprefix("app-v")
    elif raw.startswith("v"):
        version = raw.removeprefix("v")
    else:
        version = raw
    if re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version) is None:
        raise ValueError("app version must be a stable MAJOR.MINOR.PATCH value")
    return version


def _manifest_field(value: str, label: str) -> bytes:
    encoded = value.encode("utf-8")
    if not encoded or len(encoded) > 0xFFFF or "\x00" in value:
        raise ValueError(f"{label} must contain 1..65535 non-NUL UTF-8 bytes")
    return struct.pack("<H", len(encoded)) + encoded


def app_manifest(
    data: bytes, version_raw: str, platform: str, arch: str, role: str
) -> bytes:
    version = parse_app_version(version_raw)
    return b"".join(
        (
            APP_CONTEXT,
            _manifest_field(version, "app version"),
            _manifest_field(platform, "app platform"),
            _manifest_field(arch, "app architecture"),
            _manifest_field(role, "app asset role"),
            struct.pack("<Q", len(data)),
            hashlib.sha512(data).digest(),
        )
    )


def artifact_manifest(data: bytes, version_raw: str, target: str, role: str) -> bytes:
    version = ".".join(str(part) for part in parse_version(version_raw))
    return b"".join(
        (
            ARTIFACT_CONTEXT,
            _manifest_field(version, "artifact version"),
            _manifest_field(target, "artifact target"),
            _manifest_field(role, "artifact role"),
            struct.pack("<Q", len(data)),
            hashlib.sha512(data).digest(),
        )
    )


def openssl_sign(key: pathlib.Path, manifest: bytes) -> bytes:
    manifest_path = _temporary_manifest(manifest)
    try:
        result = subprocess.run(
            [
                "openssl",
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(key),
                "-in",
                str(manifest_path),
            ],
            capture_output=True,
            check=False,
        )
    finally:
        manifest_path.unlink(missing_ok=True)
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"OpenSSL signing failed: {error}")
    if len(result.stdout) != SIGNATURE_SIZE:
        raise RuntimeError(
            f"OpenSSL returned {len(result.stdout)} signature bytes; expected {SIGNATURE_SIZE}"
        )
    return result.stdout


def openssl_verify(public_key: pathlib.Path, manifest: bytes, signature: pathlib.Path) -> None:
    manifest_path = _temporary_manifest(manifest)
    try:
        result = subprocess.run(
            [
                "openssl",
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                str(public_key),
                "-sigfile",
                str(signature),
                "-in",
                str(manifest_path),
            ],
            capture_output=True,
            check=False,
        )
    finally:
        manifest_path.unlink(missing_ok=True)
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"signature self-check failed: {error}")


def _temporary_manifest(manifest: bytes) -> pathlib.Path:
    descriptor, raw_path = tempfile.mkstemp(prefix="kbhe-manifest-", suffix=".bin")
    path = pathlib.Path(raw_path)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(manifest)
            stream.flush()
            os.fsync(stream.fileno())
    except Exception:
        path.unlink(missing_ok=True)
        raise
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("firmware", "app", "artifact"))
    parser.add_argument("--key", type=pathlib.Path, help="private PEM for signing")
    parser.add_argument("--signature", type=pathlib.Path, help="verify-only signature")
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--version", help="required for firmware, e.g. 2.1.0")
    parser.add_argument("--platform", help="required for app assets, e.g. windows")
    parser.add_argument("--arch", help="required for app assets, e.g. x86_64")
    parser.add_argument("--target", help="required for generic artifacts; hardware/ABI target")
    parser.add_argument("--role", help="required for app/generic artifacts; normally the asset name")
    parser.add_argument("--verify-public", type=pathlib.Path)
    args = parser.parse_args()

    if args.kind in {"firmware", "app", "artifact"} and not args.version:
        parser.error("--version is required for every release signature")
    if args.kind == "app" and (not args.platform or not args.arch or not args.role):
        parser.error("--platform, --arch and --role are required for app signatures")
    if args.kind == "artifact" and (not args.role or not args.target):
        parser.error("--role and --target are required for artifact signatures")
    if bool(args.key) == bool(args.signature):
        parser.error("provide exactly one of --key (sign) or --signature (verify)")
    if args.key and not args.output:
        parser.error("--output is required when signing")
    if args.signature and not args.verify_public:
        parser.error("--verify-public is required in verify-only mode")

    data = args.input.read_bytes()
    if not data:
        raise ValueError("refusing to sign an empty asset")

    if args.kind == "firmware":
        manifest = firmware_manifest(data, args.version)
    elif args.kind == "app":
        manifest = app_manifest(data, args.version, args.platform, args.arch, args.role)
    else:
        manifest = artifact_manifest(data, args.version, args.target, args.role)

    if args.key:
        signature = openssl_sign(args.key, manifest)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(signature)
        if args.verify_public:
            openssl_verify(args.verify_public, manifest, args.output)
        print(f"signed {args.input} -> {args.output} ({len(signature)} bytes)")
    else:
        openssl_verify(args.verify_public, manifest, args.signature)
        print(f"verified {args.input} with {args.signature}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
