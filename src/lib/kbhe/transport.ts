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

export class KbheTransport {
  async listDevices(): Promise<KbheTransportDeviceInfo[]> {
    return invoke<KbheTransportDeviceInfo[]>("kbhe_list_devices");
  }

  async connect(path: string): Promise<KbheTransportConnectionState> {
    return invoke<KbheTransportConnectionState>("kbhe_connect", { path });
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
