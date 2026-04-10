import pathlib
import re
import struct
import time
import zlib


_UPDATER_TRAILER_MAGIC = 0x55445452
_UPDATER_TRAILER_MAGIC_BYTES = struct.pack("<I", _UPDATER_TRAILER_MAGIC)
_UPDATER_TRAILER_STRUCT = struct.Struct("<IIIHHI")

# Mirrors Core/Inc/updater_shared.h
_UPDATER_APP_SLOT_SIZE = 0x00050000
_UPDATER_TRAILER_RESERVED_SIZE = 0x00000100
_UPDATER_APP_MAX_IMAGE_SIZE = _UPDATER_APP_SLOT_SIZE - _UPDATER_TRAILER_RESERVED_SIZE

_KBHE_FW_VERSION_RECORD_MAGIC = 0x4B465756
_KBHE_FW_VERSION_RECORD_MAGIC_BYTES = struct.pack("<I", _KBHE_FW_VERSION_RECORD_MAGIC)
_KBHE_FW_VERSION_RECORD_STRUCT = struct.Struct("<IHH")

_DEBUG_GET_FW_PREFIX = b"\x80\xb4\x00\xaf"
_DEBUG_GET_FW_SUFFIX = b"\x18\x46\xbd\x46\x5d\xf8\x04\x7b\x70\x47"
_RELEASE_NEXT_FN_PREFIXES = (
    b"\x00\x00\x02\x4b\x18\x7a",
    b"\x02\x4b\x18\x7a",
    b"\x00\x00\x80\xb4\x00\xaf",
    b"\x80\xb4\x00\xaf",
)


def _read_repo_firmware_version() -> int | None:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    settings_path = repo_root / "Core" / "Src" / "settings.c"
    if not settings_path.exists():
        return None

    text = settings_path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"#define\s+FIRMWARE_VERSION\s+(0x[0-9A-Fa-f]+|\d+)", text)
    if not match:
        return None

    return int(match.group(1), 0)


def format_firmware_version(version: int) -> str:
    major = (int(version) >> 8) & 0xFF
    minor = int(version) & 0xFF
    return f"{major}.{minor}"


def _thumb_expand_imm12(imm12: int) -> int:
    imm12 &= 0xFFF
    if (imm12 >> 10) == 0:
        mode = (imm12 >> 8) & 0x3
        imm8 = imm12 & 0xFF
        if mode == 0:
            return imm8
        if mode == 1:
            return (imm8 << 16) | imm8
        if mode == 2:
            return (imm8 << 24) | (imm8 << 8)
        return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8

    unrotated = 0x80 | (imm12 & 0x7F)
    rotate = (imm12 >> 7) & 0x1F
    if rotate == 0:
        return unrotated
    return ((unrotated >> rotate) | (unrotated << (32 - rotate))) & 0xFFFFFFFF


def _decode_thumb_immediate_move(instruction: bytes) -> tuple[int, int] | None:
    if len(instruction) != 4:
        return None

    hw1 = instruction[0] | (instruction[1] << 8)
    hw2 = instruction[2] | (instruction[3] << 8)

    # MOV (immediate, modified immediate): 4F F0 / 4F F4 xx xx
    if instruction[0] == 0x4F and instruction[1] in (0xF0, 0xF4):
        rd = (hw2 >> 8) & 0xF
        i_bit = (hw1 >> 10) & 0x1
        imm3 = (hw2 >> 12) & 0x7
        imm8 = hw2 & 0xFF
        imm12 = (i_bit << 11) | (imm3 << 8) | imm8
        return rd, _thumb_expand_imm12(imm12)

    # MOVW immediate: 40 F2 xx xx .. 4F F2 xx xx
    if (hw1 & 0xFBF0) == 0xF240:
        rd = (hw2 >> 8) & 0xF
        imm4 = hw1 & 0xF
        i_bit = (hw1 >> 10) & 0x1
        imm3 = (hw2 >> 12) & 0x7
        imm8 = hw2 & 0xFF
        imm16 = (imm4 << 12) | (i_bit << 11) | (imm3 << 8) | imm8
        return rd, imm16

    return None


def _try_read_fw_version_from_image_trailer_bytes(data: bytes) -> tuple[int, str] | None:
    trailer_size = _UPDATER_TRAILER_STRUCT.size
    candidates: list[tuple[int, int]] = []

    search_pos = 0
    while True:
        trailer_offset = data.find(_UPDATER_TRAILER_MAGIC_BYTES, search_pos)
        if trailer_offset < 0:
            break
        search_pos = trailer_offset + 1

        trailer_end = trailer_offset + trailer_size
        if trailer_end > len(data):
            continue

        (
            magic,
            image_size,
            image_crc32,
            fw_version,
            _reserved,
            trailer_crc32,
        ) = _UPDATER_TRAILER_STRUCT.unpack(data[trailer_offset:trailer_end])

        if magic != _UPDATER_TRAILER_MAGIC:
            continue
        if image_size == 0 or image_size > trailer_offset:
            continue

        computed_trailer_crc = zlib.crc32(data[trailer_offset: trailer_end - 4]) & 0xFFFFFFFF
        if computed_trailer_crc != trailer_crc32:
            continue

        computed_image_crc = zlib.crc32(data[:image_size]) & 0xFFFFFFFF
        if computed_image_crc != image_crc32:
            continue

        candidates.append((trailer_offset, int(fw_version)))

    if not candidates:
        return None

    for trailer_offset, fw_version in candidates:
        if trailer_offset == _UPDATER_APP_MAX_IMAGE_SIZE:
            return fw_version, f"binary trailer @ 0x{trailer_offset:08X}"

    trailer_offset, fw_version = max(candidates, key=lambda item: item[0])
    return fw_version, f"binary trailer @ 0x{trailer_offset:08X}"


def _try_read_fw_version_from_metadata_bytes(data: bytes) -> tuple[int, str] | None:
    candidates: list[tuple[int, int]] = []
    search_pos = 0

    while True:
        record_offset = data.find(_KBHE_FW_VERSION_RECORD_MAGIC_BYTES, search_pos)
        if record_offset < 0:
            break
        search_pos = record_offset + 1

        record_end = record_offset + _KBHE_FW_VERSION_RECORD_STRUCT.size
        if record_end > len(data):
            continue

        magic, version, version_xor = _KBHE_FW_VERSION_RECORD_STRUCT.unpack(
            data[record_offset:record_end]
        )
        if magic != _KBHE_FW_VERSION_RECORD_MAGIC:
            continue
        if (version ^ version_xor) != 0xFFFF:
            continue

        candidates.append((record_offset, int(version)))

    if not candidates:
        return None

    versions = {version for _offset, version in candidates}
    if len(versions) > 1:
        formatted = ", ".join(f"0x{v:04X}" for v in sorted(versions))
        raise RuntimeError(f"ambiguous firmware version metadata in binary: {formatted}")

    record_offset, version = candidates[0]
    return version, f"binary metadata @ 0x{record_offset:08X}"


def _try_read_fw_version_from_code_signature(data: bytes) -> tuple[int, str] | None:
    strong_candidates: list[tuple[int, str, int]] = []

    # Debug-style settings_get_firmware_version()
    search_pos = 0
    while True:
        fn_offset = data.find(_DEBUG_GET_FW_PREFIX, search_pos)
        if fn_offset < 0:
            break
        search_pos = fn_offset + 1

        mov_offset = fn_offset + len(_DEBUG_GET_FW_PREFIX)
        decoded = _decode_thumb_immediate_move(data[mov_offset: mov_offset + 4])
        if decoded is None:
            continue

        rd, version = decoded
        if not (0 <= version <= 0xFFFF):
            continue

        suffix_offset = mov_offset + 4
        if rd == 3 and data[suffix_offset: suffix_offset + len(_DEBUG_GET_FW_SUFFIX)] == _DEBUG_GET_FW_SUFFIX:
            strong_candidates.append(
                (version, "binary code signature (settings getter, debug)", fn_offset)
            )

    # Release-style settings_get_firmware_version()
    release_candidates: list[tuple[int, int]] = []
    for mov_offset in range(0, len(data) - 6):
        decoded = _decode_thumb_immediate_move(data[mov_offset: mov_offset + 4])
        if decoded is None:
            continue

        rd, version = decoded
        if rd != 0 or not (0 <= version <= 0xFFFF):
            continue
        if data[mov_offset + 4: mov_offset + 6] != b"\x70\x47":
            continue

        release_candidates.append((version, mov_offset))

        follow = data[mov_offset + 6: mov_offset + 16]
        if any(follow.startswith(prefix) for prefix in _RELEASE_NEXT_FN_PREFIXES):
            strong_candidates.append(
                (version, "binary code signature (settings getter, release)", mov_offset)
            )

    if strong_candidates:
        versions = {version for version, _source, _offset in strong_candidates}
        if len(versions) > 1:
            formatted = ", ".join(f"0x{v:04X}" for v in sorted(versions))
            raise RuntimeError(f"ambiguous firmware version candidates in binary: {formatted}")
        version, source, _offset = strong_candidates[0]
        return version, source

    if len(release_candidates) == 1:
        version, _offset = release_candidates[0]
        return version, "binary code signature (single return-immediate)"

    if len(release_candidates) > 1:
        versions = {version for version, _offset in release_candidates}
        if len(versions) == 1:
            version = next(iter(versions))
            return version, "binary code signature (consistent return-immediate)"
        formatted = ", ".join(f"0x{v:04X}" for v in sorted(versions))
        raise RuntimeError(f"ambiguous firmware version candidates in binary: {formatted}")

    return None


def resolve_firmware_version(firmware_path: str | pathlib.Path, explicit_version: int | None = None):
    """Resolve firmware version used for flashing.

    Returns (version, source_label).
    """
    if explicit_version is not None:
        version = int(explicit_version)
        return version, "manual"

    data = pathlib.Path(firmware_path).read_bytes()

    trailer = _try_read_fw_version_from_image_trailer_bytes(data)
    if trailer is not None:
        return trailer

    metadata = _try_read_fw_version_from_metadata_bytes(data)
    if metadata is not None:
        return metadata

    code_signature_error: RuntimeError | None = None

    try:
        code_signature = _try_read_fw_version_from_code_signature(data)
    except RuntimeError as exc:
        code_signature_error = exc
    else:
        if code_signature is not None:
            return code_signature

    repo_version = _read_repo_firmware_version()
    if repo_version is not None:
        suffix = "after ambiguous binary signature" if code_signature_error is not None else "repo source fallback"
        return repo_version, f"Core/Src/settings.c ({suffix})"

    if code_signature_error is not None:
        raise code_signature_error

    raise RuntimeError(
        "could not detect firmware version from binary: no valid updater trailer "
        "metadata, or settings_get_firmware_version code signature found"
    )

def perform_firmware_update(device, firmware_path, firmware_version=None,
                            timeout_s=5.0, retries=5, reconnect_after=True, logger=None):
    import firmware_updater

    firmware_version, version_source = resolve_firmware_version(firmware_path, firmware_version)

    log = logger or print
    log(
        f"Flashing {firmware_path} with firmware version "
        f"{firmware_updater.format_fw_version(firmware_version)} "
        f"(0x{firmware_version:04X})"
    )
    if version_source != "manual":
        log(f"Version source: {version_source}")

    if device is not None:
        device.disconnect()

    firmware_updater.flash_firmware(
        firmware_path,
        firmware_version,
        timeout_s,
        retries,
        logger=log,
    )

    return firmware_version


def reconnect_device(device, timeout_s=5.0, logger=None):
    log = logger or print
    deadline = time.time() + timeout_s
    last_error = None

    device.disconnect()

    while time.time() < deadline:
        try:
            device.connect(logger=log)
            log("Reconnected to application Raw HID interface.")
            return True
        except Exception as exc:
            last_error = exc
            time.sleep(0.2)

    if last_error is not None:
        raise last_error

    raise RuntimeError("device did not reconnect")
