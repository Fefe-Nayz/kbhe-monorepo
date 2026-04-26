import { Command, PACKET_SIZE, REPORT_ID, formatFirmwareVersion, u16le, u32le } from "./protocol";
import { kbheCommander, KbheCommander } from "./commander";
import { kbheTransport, type KbheTransport, type KbheTransportDeviceInfo } from "./transport";

const UPDATER_TRAILER_MAGIC = 0x55445452;
const KBHE_FW_VERSION_RECORD_MAGIC = 0x4b465756;
const UPDATER_APP_SLOT_SIZE = 0x00050000;
const UPDATER_TRAILER_RESERVED_SIZE = 0x00000100;
const UPDATER_APP_MAX_IMAGE_SIZE = UPDATER_APP_SLOT_SIZE - UPDATER_TRAILER_RESERVED_SIZE;
const PROTOCOL_VERSION = 0x0001;
const FLASH_WRITE_ALIGN = 4;
const DATA_CHUNK_SIZE = 56;
const APP_CMD_ENTER_BOOTLOADER = Command.ENTER_BOOTLOADER;

const UPDATER_CMD_HELLO = 0x01;
const UPDATER_CMD_BEGIN = 0x02;
const UPDATER_CMD_DATA = 0x03;
const UPDATER_CMD_FINISH = 0x04;
const UPDATER_CMD_ABORT = 0x05;
const UPDATER_CMD_BOOT = 0x06;
const UPDATER_STATUS_OK = 0x00;

const STATUS_NAMES: Record<number, string> = {
  0x00: "OK",
  0x01: "ERROR",
  0x02: "INVALID_COMMAND",
  0x03: "INVALID_PARAMETER",
  0x04: "INVALID_STATE",
  0x05: "VERIFY_FAILED",
  0x06: "INVALID_IMAGE",
};

export interface FirmwareResolveResult {
  version: number;
  source: string;
}

export interface FirmwareFlashOptions {
  firmwareVersion?: number;
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

function parseUpdaterResponse(response: Uint8Array, expectedCommand?: number): UpdaterResponse {
  if (!response || response.length < 8) {
    throw new Error("short or empty response from updater");
  }

  const candidates: UpdaterResponse[] = [];
  for (const baseOffset of [0, 1]) {
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
  return {
    protocolVersion: u16le(payload, 0),
    flags: u16le(payload, 2),
    appBase: bytesToUint32(payload, 4),
    appMaxSize: bytesToUint32(payload, 8),
    writeAlign: bytesToUint32(payload, 12),
    installedFwVersion: u16le(payload, 16),
    reserved: u16le(payload, 18),
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

function tryReadVersionFromImageTrailer(bytes: Uint8Array): FirmwareResolveResult | null {
  const trailerSize = 20;
  let bestOffset = -1;
  let bestVersion = 0;

  for (let offset = 0; offset + trailerSize <= bytes.length; offset += 1) {
    if (bytesToUint32(bytes, offset) !== UPDATER_TRAILER_MAGIC) {
      continue;
    }

    const imageSize = bytesToUint32(bytes, offset + 4);
    const imageCrc32 = bytesToUint32(bytes, offset + 8);
    const fwVersion = u16le(bytes, offset + 12);
    const trailerCrc32 = bytesToUint32(bytes, offset + 16);

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

  if (bestOffset >= 0) {
    return { version: bestVersion, source: `binary trailer @ 0x${bestOffset.toString(16).padStart(8, "0")}` };
  }
  return null;
}

function tryReadVersionFromMetadata(bytes: Uint8Array): FirmwareResolveResult | null {
  let found: number | null = null;
  let foundOffset = -1;

  for (let offset = 0; offset + 8 <= bytes.length; offset += 1) {
    if (bytesToUint32(bytes, offset) !== KBHE_FW_VERSION_RECORD_MAGIC) {
      continue;
    }
    const version = u16le(bytes, offset + 4);
    const versionXor = u16le(bytes, offset + 6);
    if (((version ^ versionXor) & 0xffff) !== 0xffff) {
      continue;
    }
    if (found !== null && found !== version) {
      throw new Error(
        `ambiguous firmware version metadata in binary: 0x${found.toString(16).padStart(4, "0")}, 0x${version.toString(16).padStart(4, "0")}`,
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
  explicitVersion?: number,
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
    timeoutMs: number,
    log: (message: string) => void,
  ): Promise<void> {
    await this.transport.connect(runtimePath);
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

  private async findRuntimeDevice(): Promise<KbheTransportDeviceInfo | null> {
    const devices = await this.transport.listDevices();
    return devices.find((device) => device.kind === "runtime") ?? null;
  }

  private async findUpdaterDevice(): Promise<KbheTransportDeviceInfo | null> {
    const devices = await this.transport.listDevices();
    return devices.find((device) => device.kind === "updater") ?? null;
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

      log(`Retry ${attempt}/${retries} after timeout...`);
    }
    throw lastError ?? new Error("device did not respond after retries");
  }

  async flashFirmware(
    firmware: ArrayBuffer | Uint8Array,
    options: FirmwareFlashOptions = {},
  ): Promise<number> {
    const bytes = firmware instanceof Uint8Array ? firmware : new Uint8Array(firmware);
    if (bytes.length === 0) {
      throw new Error("firmware file is empty");
    }

    const { version: firmwareVersion, source } = resolveFirmwareVersion(bytes, options.firmwareVersion);
    const timeoutMs = options.timeoutMs ?? 5000;
    const retries = options.retries ?? 5;
    const log = options.onLog ?? (() => undefined);
    const transitionTimeoutMs = Math.max(timeoutMs, 12000);

    log(`Flashing image with firmware version ${formatFirmwareVersion(firmwareVersion)} (0x${firmwareVersion.toString(16).padStart(4, "0")})`);
    if (source !== "manual") {
      log(`Version source: ${source}`);
    }

    const paddedSize = alignUp(bytes.length, FLASH_WRITE_ALIGN);
    const padded = new Uint8Array(paddedSize).fill(0xff);
    padded.set(bytes);
    const imageCrc32 = crc32(bytes);

    const runtime = await this.findRuntimeDevice();
    const updater = await this.findUpdaterDevice();
    if (!updater && runtime) {
      await this.requestBootloaderTransition(runtime.path, Math.min(transitionTimeoutMs, 1200), log);
      await new Promise((resolve) => setTimeout(resolve, 250));

      let runtimeDisconnected = await this.transport.waitForDisconnect("runtime", transitionTimeoutMs);
      if (!runtimeDisconnected) {
        log("Runtime device still present; retrying ENTER_BOOTLOADER once...");
        const retryRuntime = await this.findRuntimeDevice();
        if (retryRuntime) {
          await this.requestBootloaderTransition(
            retryRuntime.path,
            Math.min(transitionTimeoutMs, 1200),
            log,
          );
          await new Promise((resolve) => setTimeout(resolve, 250));
          runtimeDisconnected = await this.transport.waitForDisconnect("runtime", transitionTimeoutMs);
        }
      }

      if (!runtimeDisconnected) {
        throw new Error("runtime device did not enter bootloader mode in time");
      }
    }

    const updaterDevice =
      (await this.transport.waitForDevice("updater", transitionTimeoutMs))
      ?? (await this.findUpdaterDevice());
    if (!updaterDevice) {
      throw new Error(`updater device not found after ${transitionTimeoutMs}ms`);
    }

    await this.transport.connect(updaterDevice.path);
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
      beginPayload[8] = firmwareVersion & 0xff;
      beginPayload[9] = (firmwareVersion >> 8) & 0xff;
      const begin = await this.transactWithRetry(
        buildUpdaterPacket(UPDATER_CMD_BEGIN, sequence, 0, beginPayload),
        timeoutMs,
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
        timeoutMs,
        retries,
        log,
        UPDATER_CMD_FINISH,
        sequence,
      );
      requireUpdaterOk(finish, UPDATER_CMD_FINISH);

      sequence = (sequence + 1) & 0xff;
      const boot = await this.transactWithRetry(
        buildUpdaterPacket(UPDATER_CMD_BOOT, sequence),
        timeoutMs,
        retries,
        log,
        UPDATER_CMD_BOOT,
        sequence,
      );
      requireUpdaterOk(boot, UPDATER_CMD_BOOT);
    } catch (error) {
      try {
        sequence = (sequence + 1) & 0xff;
        await this.transport.writeReport(buildUpdaterPacket(UPDATER_CMD_ABORT, sequence));
      } catch {
        // ignore abort failures
      }
      throw error;
    } finally {
      await this.transport.disconnect();
    }

    await this.transport.waitForDisconnect("updater", Math.max(timeoutMs, 5000));
    const appDevice = await this.transport.waitForDevice("runtime", Math.max(timeoutMs, 15000));
    if (appDevice) {
      await this.transport.connect(appDevice.path);
    }
    log("Update complete, application is back online.");
    return firmwareVersion;
  }
}

export const kbheFirmware = new KBHEFirmware();
