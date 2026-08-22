import {
  Command,
  PACKET_SIZE,
  REPORT_ID,
  formatFirmwareVersion,
  u16le,
  u32le,
  type FirmwareVersion,
} from "./protocol";
import { kbheCommander, KbheCommander } from "./commander";
import { kbheTransport, type KbheTransport, type KbheTransportDeviceInfo } from "./transport";

const UPDATER_TRAILER_MAGIC = 0x55445452;
const KBHE_FW_VERSION_RECORD_MAGIC = 0x4b465756;
const UPDATER_APP_SLOT_SIZE = 0x00030000;
const UPDATER_APP_BASE = 0x08010000;
const UPDATER_TRAILER_RESERVED_SIZE = 0x00000100;
const UPDATER_APP_MAX_IMAGE_SIZE = UPDATER_APP_SLOT_SIZE - UPDATER_TRAILER_RESERVED_SIZE;
const PROTOCOL_VERSION = 0x0003;
const FLASH_WRITE_ALIGN = 4;
const DATA_CHUNK_SIZE = 56;
const BEGIN_MIN_TIMEOUT_MS = 6000;
const APP_CMD_ENTER_BOOTLOADER = Command.ENTER_BOOTLOADER;

const UPDATER_CMD_HELLO = 0x01;
const UPDATER_CMD_BEGIN = 0x02;
const UPDATER_CMD_DATA = 0x03;
const UPDATER_CMD_FINISH = 0x04;
const UPDATER_CMD_ABORT = 0x05;
const UPDATER_CMD_BOOT = 0x06;
const UPDATER_CMD_AUTH = 0x07;
const UPDATER_STATUS_OK = 0x00;
const UPDATER_FLAG_SIGNATURE_REQUIRED = 1 << 2;
const FIRMWARE_SIGNATURE_SIZE = 64;

const STATUS_NAMES: Record<number, string> = {
  0x00: "OK",
  0x01: "ERROR",
  0x02: "INVALID_COMMAND",
  0x03: "INVALID_PARAMETER",
  0x04: "INVALID_STATE",
  0x05: "VERIFY_FAILED",
  0x06: "INVALID_IMAGE",
  0x07: "AUTH_REQUIRED",
  0x08: "AUTH_FAILED",
  0x09: "ROLLBACK_REJECTED",
  0x0a: "STORAGE_ERROR",
};

export interface FirmwareResolveResult {
  version: FirmwareVersion;
  source: string;
}

export interface FirmwareFlashOptions {
  firmwareVersion?: FirmwareVersion;
  signature?: ArrayBuffer | Uint8Array;
  expectedSerialNumber: string;
  timeoutMs?: number;
  retries?: number;
  onLog?: (message: string) => void;
  onProgress?: (progress: { written: number; total: number; percent: number }) => void;
}

interface UpdaterResponse {
  command: number;
  sequence: number;
  status: number;
  length: number;
  offset: number;
  payload: Uint8Array;
}

function alignUp(value: number, align: number): number {
  return (value + align - 1) & ~(align - 1);
}

function bytesToUint32(bytes: Uint8Array, offset: number): number {
  return u32le(bytes, offset);
}

function buildUpdaterPacket(
  command: number,
  sequence: number,
  offset = 0,
  payload: ArrayLike<number> = [],
): Uint8Array {
  if (payload.length > DATA_CHUNK_SIZE) {
    throw new Error("payload too large");
  }

  const packet = new Uint8Array(PACKET_SIZE + 1);
  packet[0] = REPORT_ID;
  packet[1] = command & 0xff;
  packet[2] = sequence & 0xff;
  packet[3] = 0;
  packet[4] = payload.length & 0xff;
  packet[5] = offset & 0xff;
  packet[6] = (offset >> 8) & 0xff;
  packet[7] = (offset >> 16) & 0xff;
  packet[8] = (offset >> 24) & 0xff;
  for (let index = 0; index < payload.length; index += 1) {
    packet[9 + index] = payload[index] ?? 0;
  }
  return packet;
}

export function parseUpdaterResponse(response: Uint8Array, expectedCommand?: number): UpdaterResponse {
  if (response.length !== PACKET_SIZE && response.length !== PACKET_SIZE + 1) {
    throw new Error(
      `invalid updater report size ${response.length}; expected ${PACKET_SIZE} or ${PACKET_SIZE + 1}`,
    );
  }

  const candidates: UpdaterResponse[] = [];
  const baseOffsets = response.length === PACKET_SIZE + 1 ? [1] : [0];
  if (baseOffsets[0] === 1 && response[0] !== REPORT_ID) {
    throw new Error("invalid updater report ID");
  }
  for (const baseOffset of baseOffsets) {
    if (response.length < baseOffset + 8) {
      continue;
    }

    const length = response[baseOffset + 3];
    if (length > DATA_CHUNK_SIZE) {
      continue;
    }

    const payloadStart = baseOffset + 8;
    const payloadEnd = payloadStart + length;
    if (payloadEnd > response.length) {
      continue;
    }

    candidates.push({
      command: response[baseOffset],
      sequence: response[baseOffset + 1],
      status: response[baseOffset + 2],
      length,
      offset: bytesToUint32(response, baseOffset + 4),
      payload: response.slice(payloadStart, payloadEnd),
    });
  }

  if (candidates.length === 0) {
    throw new Error("invalid updater response header");
  }

  if (expectedCommand !== undefined) {
    const matched = candidates.find((candidate) => candidate.command === expectedCommand);
    if (matched) {
      return matched;
    }
  }

  return candidates[0]!;
}

function requireUpdaterOk(response: UpdaterResponse, expectedCommand: number): void {
  if (response.command !== expectedCommand) {
    throw new Error(
      `unexpected response command 0x${response.command.toString(16)}, expected 0x${expectedCommand.toString(16)}`,
    );
  }
  if (response.status !== UPDATER_STATUS_OK) {
    throw new Error(STATUS_NAMES[response.status] ?? `0x${response.status.toString(16)}`);
  }
}

function parseHelloPayload(payload: Uint8Array) {
  if (payload.length < 20) {
    throw new Error("HELLO payload too short");
  }
  const installedFwVersion: FirmwareVersion = {
    major: payload[16] ?? 0,
    minor: payload[17] ?? 0,
    patch: payload[18] ?? 0,
  };
  const installedPresent =
    installedFwVersion.major !== 0 ||
    installedFwVersion.minor !== 0 ||
    installedFwVersion.patch !== 0;
  return {
    protocolVersion: u16le(payload, 0),
    flags: u16le(payload, 2),
    appBase: bytesToUint32(payload, 4),
    appMaxSize: bytesToUint32(payload, 8),
    writeAlign: bytesToUint32(payload, 12),
    installedFwVersion: installedPresent ? installedFwVersion : null,
  };
}

function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      const mask = -(crc & 1);
      crc = (crc >>> 1) ^ (0xedb88320 & mask);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function selectFirmwareTargetDevice(
  devices: KbheTransportDeviceInfo[],
  kind: KbheTransportDeviceInfo["kind"],
  expectedSerialNumber: string,
): KbheTransportDeviceInfo | null {
  const expected = expectedSerialNumber.trim();
  if (!expected) {
    throw new Error("firmware flashing requires the serial number of the connected keyboard");
  }

  const matches = devices.filter(
    (device) => device.kind === kind && device.serialNumber?.trim() === expected,
  );
  if (matches.length > 1) {
    throw new Error(
      `refusing ambiguous firmware target: found ${matches.length} ${kind} devices with serial ${expected}`,
    );
  }
  return matches[0] ?? null;
}

export function resolveFirmwareTargetSnapshot(
  devices: KbheTransportDeviceInfo[],
  expectedSerialNumber: string,
): { runtime: KbheTransportDeviceInfo | null; updater: KbheTransportDeviceInfo | null } {
  const runtime = selectFirmwareTargetDevice(devices, "runtime", expectedSerialNumber);
  const updater = selectFirmwareTargetDevice(devices, "updater", expectedSerialNumber);
  if (runtime && updater) {
    throw new Error(
      `refusing ambiguous firmware target: serial ${expectedSerialNumber.trim()} is present in both runtime and updater mode`,
    );
  }
  return { runtime, updater };
}

export async function buildFirmwareSignatureManifest(
  bytes: Uint8Array,
  version: FirmwareVersion,
  imageCrc32: number,
): Promise<Uint8Array> {
  const digest = new Uint8Array(
    await crypto.subtle.digest("SHA-512", bytes.slice().buffer),
  );
  const manifest = new Uint8Array(84);
  manifest.set(new TextEncoder().encode("KBHEFW3\0"), 0);
  const view = new DataView(manifest.buffer);
  view.setUint32(8, bytes.length, true);
  view.setUint32(12, imageCrc32, true);
  manifest[16] = version.major & 0xff;
  manifest[17] = version.minor & 0xff;
  manifest[18] = version.patch & 0xff;
  manifest[19] = 0;
  manifest.set(digest, 20);
  return manifest;
}

function tryReadVersionFromImageTrailer(bytes: Uint8Array): FirmwareResolveResult | null {
  const trailerSize = 84;
  let bestOffset = -1;
  let bestVersion: FirmwareVersion | null = null;

  for (let offset = 0; offset + trailerSize <= bytes.length; offset += 1) {
    if (bytesToUint32(bytes, offset) !== UPDATER_TRAILER_MAGIC) {
      continue;
    }

    const imageSize = bytesToUint32(bytes, offset + 4);
    const imageCrc32 = bytesToUint32(bytes, offset + 8);
    const fwVersion: FirmwareVersion = {
      major: bytes[offset + 12] ?? 0,
      minor: bytes[offset + 13] ?? 0,
      patch: bytes[offset + 14] ?? 0,
    };
    const trailerCrc32 = bytesToUint32(bytes, offset + 80);

    if (imageSize === 0 || imageSize > offset) {
      continue;
    }

    if (crc32(bytes.slice(offset, offset + trailerSize - 4)) !== trailerCrc32) {
      continue;
    }

    if (crc32(bytes.slice(0, imageSize)) !== imageCrc32) {
      continue;
    }

    if (offset === UPDATER_APP_MAX_IMAGE_SIZE) {
      return { version: fwVersion, source: `binary trailer @ 0x${offset.toString(16).padStart(8, "0")}` };
    }

    if (offset > bestOffset) {
      bestOffset = offset;
      bestVersion = fwVersion;
    }
  }

  if (bestVersion) {
    return { version: bestVersion, source: `binary trailer @ 0x${bestOffset.toString(16).padStart(8, "0")}` };
  }
  return null;
}

function tryReadVersionFromMetadata(bytes: Uint8Array): FirmwareResolveResult | null {
  let found: FirmwareVersion | null = null;
  let foundOffset = -1;

  for (let offset = 0; offset + 12 <= bytes.length; offset += 1) {
    if (bytesToUint32(bytes, offset) !== KBHE_FW_VERSION_RECORD_MAGIC) {
      continue;
    }
    const versionPacked = bytesToUint32(bytes, offset + 4);
    const versionXor = bytesToUint32(bytes, offset + 8);
    if (((versionPacked ^ versionXor) >>> 0) !== 0xffffffff) {
      continue;
    }
    const version: FirmwareVersion = {
      major: (versionPacked >>> 16) & 0xff,
      minor: (versionPacked >>> 8) & 0xff,
      patch: versionPacked & 0xff,
    };
    if (
      found !== null &&
      (found.major !== version.major ||
        found.minor !== version.minor ||
        found.patch !== version.patch)
    ) {
      throw new Error(
        `ambiguous firmware version metadata in binary: ${formatFirmwareVersion(found)}, ${formatFirmwareVersion(version)}`,
      );
    }
    found = version;
    foundOffset = offset;
  }

  return found !== null
    ? { version: found, source: `binary metadata @ 0x${foundOffset.toString(16).padStart(8, "0")}` }
    : null;
}

export function resolveFirmwareVersion(
  firmware: ArrayBuffer | Uint8Array,
  explicitVersion?: FirmwareVersion,
): FirmwareResolveResult {
  if (explicitVersion !== undefined) {
    return { version: explicitVersion, source: "manual" };
  }

  const bytes = firmware instanceof Uint8Array ? firmware : new Uint8Array(firmware);
  const trailer = tryReadVersionFromImageTrailer(bytes);
  if (trailer) {
    return trailer;
  }

  const metadata = tryReadVersionFromMetadata(bytes);
  if (metadata) {
    return metadata;
  }

  throw new Error("could not detect firmware version from binary");
}

export class KBHEFirmware {
  constructor(
    private readonly transport: KbheTransport = kbheTransport,
    private readonly commander: KbheCommander = kbheCommander,
  ) {}

  private async requestBootloaderTransition(
    runtimePath: string,
    expectedSerialNumber: string,
    timeoutMs: number,
    log: (message: string) => void,
  ): Promise<void> {
    await this.transport.connect(runtimePath, expectedSerialNumber);
    try {
      // Some boards disconnect before replying to ENTER_BOOTLOADER.
      await this.commander.sendCommand(APP_CMD_ENTER_BOOTLOADER, [], timeoutMs);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      log(`No ENTER_BOOTLOADER ACK (${message}); waiting for USB re-enumeration...`);
    } finally {
      try {
        await this.transport.disconnect();
      } catch {
        // Ignore disconnect errors while transitioning to updater mode.
      }
    }
  }

  private async findRuntimeDevice(expectedSerialNumber: string): Promise<KbheTransportDeviceInfo | null> {
    return selectFirmwareTargetDevice(
      await this.transport.listDevices(),
      "runtime",
      expectedSerialNumber,
    );
  }

  private async findUpdaterDevice(expectedSerialNumber: string): Promise<KbheTransportDeviceInfo | null> {
    return selectFirmwareTargetDevice(
      await this.transport.listDevices(),
      "updater",
      expectedSerialNumber,
    );
  }

  private async waitForDevice(
    kind: KbheTransportDeviceInfo["kind"],
    expectedSerialNumber: string,
    timeoutMs: number,
  ): Promise<KbheTransportDeviceInfo | null> {
    const deadline = Date.now() + timeoutMs;
    do {
      const device = selectFirmwareTargetDevice(
        await this.transport.listDevices(),
        kind,
        expectedSerialNumber,
      );
      if (device) {
        return device;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    } while (Date.now() < deadline);
    return null;
  }

  private async waitForDeviceAbsent(
    kind: KbheTransportDeviceInfo["kind"],
    expectedSerialNumber: string,
    timeoutMs: number,
  ): Promise<boolean> {
    const deadline = Date.now() + timeoutMs;
    do {
      const device = selectFirmwareTargetDevice(
        await this.transport.listDevices(),
        kind,
        expectedSerialNumber,
      );
      if (!device) {
        return true;
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    } while (Date.now() < deadline);
    return false;
  }

  private async transactWithRetry(
    report: Uint8Array,
    timeoutMs: number,
    retries: number,
    log: (message: string) => void,
    expectedCommand: number,
    expectedSequence?: number,
  ): Promise<UpdaterResponse> {
    let lastError: Error | null = null;
    for (let attempt = 1; attempt <= retries; attempt += 1) {
      let unexpectedCommand: number | null = null;
      let unexpectedSequence: number | null = null;
      try {
        try {
          await this.transport.flushInput();
        } catch {
          // Ignore flush failures and continue with the transaction.
        }

        await this.transport.writeReport(report);

        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline) {
          const remaining = Math.max(1, deadline - Date.now());
          const rawResponse = await this.transport.readReport(remaining);
          if (rawResponse.length === 0) {
            continue;
          }

          const response = parseUpdaterResponse(rawResponse, expectedCommand);
          if (response.command !== expectedCommand) {
            unexpectedCommand = response.command;
            continue;
          }

          if (expectedSequence !== undefined && response.sequence !== expectedSequence) {
            unexpectedSequence = response.sequence;
            continue;
          }

          return response;
        }
      } catch (error) {
        lastError = error instanceof Error ? error : new Error(String(error));
      }

      if (unexpectedCommand !== null) {
        log(
          `Ignoring response command 0x${unexpectedCommand.toString(16)} while waiting for 0x${expectedCommand.toString(16)}`,
        );
      }
      if (unexpectedSequence !== null) {
        log(
          `Ignoring response sequence 0x${unexpectedSequence.toString(16)} while waiting for 0x${(expectedSequence ?? 0).toString(16)}`,
        );
      }

      if (attempt < retries) {
        log(`Retry ${attempt}/${retries} after timeout...`);
      }
    }
    throw lastError ?? new Error("device did not respond after retries");
  }

  async flashFirmware(
    firmware: ArrayBuffer | Uint8Array,
    options: FirmwareFlashOptions,
  ): Promise<FirmwareVersion> {
    const bytes = firmware instanceof Uint8Array ? firmware : new Uint8Array(firmware);
    if (bytes.length === 0) {
      throw new Error("firmware file is empty");
    }
    const signature = options.signature instanceof Uint8Array
      ? options.signature
      : options.signature
        ? new Uint8Array(options.signature)
        : null;
    if (!signature || signature.length !== FIRMWARE_SIGNATURE_SIZE) {
      throw new Error(`a ${FIRMWARE_SIGNATURE_SIZE}-byte Ed25519 firmware signature is required`);
    }
    const expectedSerialNumber = options.expectedSerialNumber.trim();
    if (!expectedSerialNumber) {
      throw new Error("firmware flashing requires the serial number of the connected keyboard");
    }

    const { version: firmwareVersion, source } = resolveFirmwareVersion(bytes, options.firmwareVersion);
    const timeoutMs = options.timeoutMs ?? 5000;
    const retries = options.retries ?? 5;
    const log = options.onLog ?? (() => undefined);
    const transitionTimeoutMs = Math.max(timeoutMs, 12000);

    log(`Flashing image with firmware version ${formatFirmwareVersion(firmwareVersion)}`);
    if (source !== "manual") {
      log(`Version source: ${source}`);
    }

    const paddedSize = alignUp(bytes.length, FLASH_WRITE_ALIGN);
    const padded = new Uint8Array(paddedSize).fill(0xff);
    padded.set(bytes);
    const imageCrc32 = crc32(bytes);
    const signedManifest = await buildFirmwareSignatureManifest(
      bytes,
      firmwareVersion,
      imageCrc32,
    );
    const authorization = new Uint8Array(signedManifest.length + signature.length);
    authorization.set(signedManifest, 0);
    authorization.set(signature, signedManifest.length);

    const initialDevices = await this.transport.listDevices();
    const { runtime, updater } = resolveFirmwareTargetSnapshot(
      initialDevices,
      expectedSerialNumber,
    );
    if (!runtime && !updater) {
      throw new Error(
        `keyboard serial ${expectedSerialNumber} was not found in runtime or updater mode`,
      );
    }
    if (!updater && runtime) {
      await this.requestBootloaderTransition(
        runtime.path,
        expectedSerialNumber,
        Math.min(transitionTimeoutMs, 1200),
        log,
      );
      await new Promise((resolve) => setTimeout(resolve, 250));

      let runtimeDisconnected = await this.waitForDeviceAbsent(
        "runtime",
        expectedSerialNumber,
        transitionTimeoutMs,
      );
      if (!runtimeDisconnected) {
        log("Runtime device still present; retrying ENTER_BOOTLOADER once...");
        const retryRuntime = await this.findRuntimeDevice(expectedSerialNumber);
        if (retryRuntime) {
          await this.requestBootloaderTransition(
            retryRuntime.path,
            expectedSerialNumber,
            Math.min(transitionTimeoutMs, 1200),
            log,
          );
          await new Promise((resolve) => setTimeout(resolve, 250));
          runtimeDisconnected = await this.waitForDeviceAbsent(
            "runtime",
            expectedSerialNumber,
            transitionTimeoutMs,
          );
        }
      }

      if (!runtimeDisconnected) {
        throw new Error("runtime device did not enter bootloader mode in time");
      }
    }

    const updaterDevice =
      (await this.waitForDevice("updater", expectedSerialNumber, transitionTimeoutMs))
      ?? (await this.findUpdaterDevice(expectedSerialNumber));
    if (!updaterDevice) {
      throw new Error(`updater device not found after ${transitionTimeoutMs}ms`);
    }

    await this.transport.connect(updaterDevice.path, expectedSerialNumber);
    log(`Connected to updater: ${updaterDevice.path}`);

    let sequence = 1;
    try {
      const hello = await this.transactWithRetry(
        buildUpdaterPacket(UPDATER_CMD_HELLO, sequence),
        timeoutMs,
        retries,
        log,
        UPDATER_CMD_HELLO,
        sequence,
      );
      requireUpdaterOk(hello, UPDATER_CMD_HELLO);
      const helloPayload = parseHelloPayload(hello.payload);

      if (helloPayload.protocolVersion !== PROTOCOL_VERSION) {
        throw new Error(
          `unsupported updater protocol 0x${helloPayload.protocolVersion.toString(16)}, expected 0x${PROTOCOL_VERSION.toString(16)}`,
        );
      }
      if ((helloPayload.flags & UPDATER_FLAG_SIGNATURE_REQUIRED) === 0) {
        throw new Error("updater does not enforce signed firmware");
      }
      if (helloPayload.appBase !== UPDATER_APP_BASE) {
        throw new Error(
          `unexpected updater app base 0x${helloPayload.appBase.toString(16)}, expected 0x${UPDATER_APP_BASE.toString(16)}`,
        );
      }
      if (helloPayload.appMaxSize !== UPDATER_APP_MAX_IMAGE_SIZE) {
        throw new Error(
          `unexpected updater app max ${helloPayload.appMaxSize}, expected ${UPDATER_APP_MAX_IMAGE_SIZE}`,
        );
      }
      if (helloPayload.writeAlign !== FLASH_WRITE_ALIGN) {
        throw new Error(
          `unexpected flash write alignment ${helloPayload.writeAlign}, expected ${FLASH_WRITE_ALIGN}`,
        );
      }
      if (bytes.length > helloPayload.appMaxSize) {
        throw new Error(
          `firmware is too large (${bytes.length} bytes), updater max is ${helloPayload.appMaxSize} bytes`,
        );
      }

      log(
        `Updater ready: app_base=0x${helloPayload.appBase.toString(16)}, max_size=${helloPayload.appMaxSize}, installed=${helloPayload.installedFwVersion ? formatFirmwareVersion(helloPayload.installedFwVersion) : "unknown"}`,
      );

      let authOffset = 0;
      log("Authenticating signed manifest before flash erase...");
      while (authOffset < authorization.length) {
        sequence = (sequence + 1) & 0xff;
        const chunk = authorization.slice(authOffset, authOffset + DATA_CHUNK_SIZE);
        const auth = await this.transactWithRetry(
          buildUpdaterPacket(UPDATER_CMD_AUTH, sequence, authOffset, chunk),
          timeoutMs,
          retries,
          log,
          UPDATER_CMD_AUTH,
          sequence,
        );
        requireUpdaterOk(auth, UPDATER_CMD_AUTH);
        authOffset += chunk.length;
      }
      sequence = (sequence + 1) & 0xff;
      const beginPayload = new Uint8Array(12);
      beginPayload[0] = bytes.length & 0xff;
      beginPayload[1] = (bytes.length >> 8) & 0xff;
      beginPayload[2] = (bytes.length >> 16) & 0xff;
      beginPayload[3] = (bytes.length >> 24) & 0xff;
      beginPayload[4] = imageCrc32 & 0xff;
      beginPayload[5] = (imageCrc32 >> 8) & 0xff;
      beginPayload[6] = (imageCrc32 >> 16) & 0xff;
      beginPayload[7] = (imageCrc32 >> 24) & 0xff;
      beginPayload[8] = firmwareVersion.major & 0xff;
      beginPayload[9] = firmwareVersion.minor & 0xff;
      beginPayload[10] = firmwareVersion.patch & 0xff;
      const begin = await this.transactWithRetry(
        buildUpdaterPacket(UPDATER_CMD_BEGIN, sequence, 0, beginPayload),
        Math.max(timeoutMs, BEGIN_MIN_TIMEOUT_MS),
        retries,
        log,
        UPDATER_CMD_BEGIN,
        sequence,
      );
      requireUpdaterOk(begin, UPDATER_CMD_BEGIN);

      let offset = 0;
      while (offset < padded.length) {
        sequence = (sequence + 1) & 0xff;
        const chunk = padded.slice(offset, offset + DATA_CHUNK_SIZE);
        const response = await this.transactWithRetry(
          buildUpdaterPacket(UPDATER_CMD_DATA, sequence, offset, chunk),
          timeoutMs,
          retries,
          log,
          UPDATER_CMD_DATA,
          sequence,
        );
        requireUpdaterOk(response, UPDATER_CMD_DATA);

        const expectedNextOffset = offset + chunk.length;
        if (response.offset !== expectedNextOffset) {
          throw new Error(
            `updater acknowledged offset 0x${response.offset.toString(16)}, expected 0x${expectedNextOffset.toString(16)}`,
          );
        }

        offset = response.offset;
        const progress = Math.min(offset, bytes.length);
        const percent = Math.floor((progress * 100) / bytes.length);
        options.onProgress?.({ written: progress, total: bytes.length, percent });
        log(`Flashing: ${progress}/${bytes.length} bytes (${percent}%)`);
      }

      sequence = (sequence + 1) & 0xff;
      const finish = await this.transactWithRetry(
        buildUpdaterPacket(UPDATER_CMD_FINISH, sequence),
        Math.max(timeoutMs, 5000),
        retries,
        log,
        UPDATER_CMD_FINISH,
        sequence,
      );
      requireUpdaterOk(finish, UPDATER_CMD_FINISH);

      sequence = (sequence + 1) & 0xff;
      try {
        const boot = await this.transactWithRetry(
          buildUpdaterPacket(UPDATER_CMD_BOOT, sequence),
          Math.min(timeoutMs, 2000),
          Math.min(retries, 3),
          log,
          UPDATER_CMD_BOOT,
          sequence,
        );
        requireUpdaterOk(boot, UPDATER_CMD_BOOT);
      } catch {
        // A successful BOOT normally resets USB before the acknowledgement can
        // be read. Runtime re-enumeration below is the authoritative result.
        log("BOOT acknowledgement unavailable; waiting for runtime USB re-enumeration...");
      }
    } catch (error) {
      try {
        sequence = (sequence + 1) & 0xff;
        await this.transactWithRetry(
          buildUpdaterPacket(UPDATER_CMD_ABORT, sequence),
          timeoutMs,
          Math.min(retries, 2),
          log,
          UPDATER_CMD_ABORT,
          sequence,
        );
      } catch {
        // ignore abort failures
      }
      try {
        sequence = (sequence + 1) & 0xff;
        await this.transactWithRetry(
          buildUpdaterPacket(UPDATER_CMD_BOOT, sequence),
          timeoutMs,
          Math.min(retries, 2),
          log,
          UPDATER_CMD_BOOT,
          sequence,
        );
      } catch {
        // BOOT safely fails if BEGIN already erased the previous application.
      }
      throw error;
    } finally {
      await this.transport.disconnect();
    }

    await this.waitForDeviceAbsent(
      "updater",
      expectedSerialNumber,
      Math.max(timeoutMs, 5000),
    );
    const appDevice = await this.waitForDevice(
      "runtime",
      expectedSerialNumber,
      Math.max(timeoutMs, 15000),
    );
    if (!appDevice) {
      throw new Error("application did not return after the updater finished");
    }
    await this.transport.connect(appDevice.path, expectedSerialNumber);
    log("Update complete, application is back online.");
    return firmwareVersion;
  }
}

export const kbheFirmware = new KBHEFirmware();
