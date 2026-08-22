import { invoke } from "@tauri-apps/api/core";

export type KbheDeviceKind = "runtime" | "updater";

export interface KbheTransportDeviceInfo {
  path: string;
  vid: number;
  pid: number;
  kind: KbheDeviceKind;
  interfaceNumber: number | null;
  usagePage: number | null;
  usage: number | null;
  manufacturer: string | null;
  product: string | null;
  serialNumber: string | null;
}

export interface KbheTransportConnectionState {
  connected: boolean;
  path: string | null;
  pid: number | null;
  kind: KbheDeviceKind | null;
}

/**
 * Resolve one physical keyboard without relying on HID enumeration order.
 *
 * A serial is mandatory for a session target: it is the only identity that
 * survives USB path changes and runtime/updater re-enumeration.  Duplicate
 * entries for the same serial (including a transient cross-mode duplicate)
 * are rejected instead of guessing.
 */
export function selectKbheSessionDevice(
  devices: readonly KbheTransportDeviceInfo[],
  expectedSerialNumber?: string | null,
): KbheTransportDeviceInfo | null {
  const expected = expectedSerialNumber?.trim() ?? "";
  if (devices.length === 0) return null;

  const candidates = expected
    ? devices.filter((device) => device.serialNumber?.trim() === expected)
    : devices;

  if (expected && candidates.length === 0) {
    throw new Error(`keyboard serial ${expected} is not present`);
  }
  if (!expected) {
    const missingSerial = candidates.find((device) => !device.serialNumber?.trim());
    if (missingSerial) {
      throw new Error(
        "the detected keyboard exposes no stable USB serial number; refusing an unverifiable session target",
      );
    }
    const physicalSerials = new Set(candidates.map((device) => device.serialNumber!.trim()));
    if (physicalSerials.size > 1) {
      throw new Error(
        `multiple KBHE keyboards are connected (${physicalSerials.size}); select one physical keyboard before connecting`,
      );
    }
  }
  if (candidates.length !== 1) {
    const serial = expected || candidates[0]?.serialNumber?.trim() || "unknown";
    throw new Error(
      `refusing ambiguous KBHE target: found ${candidates.length} HID devices with serial ${serial}`,
    );
  }

  const candidate = candidates[0]!;
  if (!candidate.serialNumber?.trim()) {
    throw new Error(
      "the selected keyboard exposes no stable USB serial number; refusing an unverifiable session target",
    );
  }
  return candidate;
}

export function kbheDeviceStorageId(device: KbheTransportDeviceInfo): string {
  const serialNumber = device.serialNumber?.trim();
  if (serialNumber) {
    return `serial:${serialNumber}`;
  }
  return `path:${device.vid.toString(16)}:${device.pid.toString(16)}:${device.path}`;
}

export class KbheTransport {
  async listDevices(): Promise<KbheTransportDeviceInfo[]> {
    return invoke<KbheTransportDeviceInfo[]>("kbhe_list_devices");
  }

  async detectBootloaderPresence(): Promise<boolean> {
    return invoke<boolean>("kbhe_detect_bootloader_presence");
  }

  async connect(
    path: string,
    expectedSerialNumber?: string,
  ): Promise<KbheTransportConnectionState> {
    return invoke<KbheTransportConnectionState>("kbhe_connect", {
      path,
      expectedSerialNumber,
    });
  }

  async disconnect(): Promise<KbheTransportConnectionState> {
    return invoke<KbheTransportConnectionState>("kbhe_disconnect");
  }

  async connectionState(): Promise<KbheTransportConnectionState> {
    return invoke<KbheTransportConnectionState>("kbhe_connection_state");
  }

  async flushInput(): Promise<number> {
    return invoke<number>("kbhe_flush_input");
  }

  async writeReport(report: ArrayLike<number>): Promise<number> {
    return invoke<number>("kbhe_write_report", {
      report: Array.from(report, (value) => value & 0xff),
    });
  }

  async readReport(timeoutMs: number): Promise<Uint8Array> {
    const response = await invoke<number[]>("kbhe_read_report", {
      timeoutMs: Math.max(0, Math.trunc(timeoutMs)),
    });
    return Uint8Array.from(response);
  }

  async sendCommand(
    command: number,
    data: ArrayLike<number> = [],
    timeoutMs = 100,
  ): Promise<Uint8Array | null> {
    const response = await invoke<number[] | null>("kbhe_send_command", {
      command: Math.trunc(command) & 0xff,
      data: Array.from(data, (value) => value & 0xff),
      timeoutMs: Math.max(0, Math.trunc(timeoutMs)),
    });

    return response ? Uint8Array.from(response) : null;
  }

  async waitForDevice(
    kind: KbheDeviceKind,
    timeoutMs: number,
  ): Promise<KbheTransportDeviceInfo | null> {
    return invoke<KbheTransportDeviceInfo | null>("kbhe_wait_for_device", {
      kind,
      timeoutMs: Math.max(0, Math.trunc(timeoutMs)),
    });
  }

  async waitForDisconnect(kind: KbheDeviceKind, timeoutMs: number): Promise<boolean> {
    return invoke<boolean>("kbhe_wait_for_disconnect", {
      kind,
      timeoutMs: Math.max(0, Math.trunc(timeoutMs)),
    });
  }
}

export const kbheTransport = new KbheTransport();
