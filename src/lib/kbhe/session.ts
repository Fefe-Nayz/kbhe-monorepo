/**
 * DeviceSessionManager — lightweight frontend session manager over KBHE transport.
 *
 * Goals:
 *  - never block the first render with long USB waits
 *  - avoid long-running Tauri invoke calls during normal runtime
 *  - keep connection state reactive and reconnect automatically
 */

import { create } from "zustand";
import { kbheDevice } from "./device";
import type { KbheTransportDeviceInfo } from "./transport";

export type DeviceSessionStatus =
  | "disconnected"
  | "connecting"
  | "connected"
  | "updater"
  | "error";

export interface DeviceSessionState {
  status: DeviceSessionStatus;
  deviceInfo: KbheTransportDeviceInfo | null;
  firmwareVersion: string | null;
  error: string | null;
  developerMode: boolean;

  _setStatus: (status: DeviceSessionStatus) => void;
  _setDeviceInfo: (info: KbheTransportDeviceInfo | null) => void;
  _setFirmwareVersion: (v: string | null) => void;
  _setError: (e: string | null) => void;
  setDeveloperMode: (enabled: boolean) => void;
}

export const useDeviceSession = create<DeviceSessionState>()((set) => ({
  status: "disconnected",
  deviceInfo: null,
  firmwareVersion: null,
  error: null,
  developerMode: false,

  _setStatus: (status) => set({ status }),
  _setDeviceInfo: (deviceInfo) => set({ deviceInfo }),
  _setFirmwareVersion: (firmwareVersion) => set({ firmwareVersion }),
  _setError: (error) => set({ error }),
  setDeveloperMode: (developerMode) => set({ developerMode }),
}));

let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let presenceInterval: ReturnType<typeof setInterval> | null = null;
let connectPromise: Promise<void> | null = null;
let initialized = false;
let generation = 0;

function clearReconnectTimer() {
  if (reconnectTimer !== null) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
}

function stopPresencePolling() {
  if (presenceInterval !== null) {
    clearInterval(presenceInterval);
    presenceInterval = null;
  }
}

function resetDisconnectedState() {
  const { _setStatus, _setDeviceInfo, _setFirmwareVersion, _setError } = useDeviceSession.getState();
  _setStatus("disconnected");
  _setDeviceInfo(null);
  _setFirmwareVersion(null);
  _setError(null);
}

function scheduleReconnect(delayMs = 2500) {
  clearReconnectTimer();
  reconnectTimer = window.setTimeout(() => {
    void DeviceSessionManager.connect();
  }, delayMs);
}

export const DeviceSessionManager = {
  async init() {
    if (initialized) {
      return;
    }

    initialized = true;
    window.setTimeout(() => {
      void DeviceSessionManager.connect();
    }, 0);
  },

  async connect() {
    const state = useDeviceSession.getState();
    if (state.status === "connecting" || connectPromise) {
      return connectPromise ?? Promise.resolve();
    }

    const currentGeneration = ++generation;
    connectPromise = (async () => {
      const { _setStatus, _setDeviceInfo, _setFirmwareVersion, _setError } = useDeviceSession.getState();
      clearReconnectTimer();
      _setStatus("connecting");
      _setError(null);

      try {
        const devices = await kbheDevice.listDevices();
        if (currentGeneration !== generation) {
          return;
        }

        if (devices.length === 0) {
          resetDisconnectedState();
          scheduleReconnect();
          return;
        }

        const runtimeDevice = devices.find((device) => device.kind === "runtime");
        const device = runtimeDevice ?? devices[0]!;

        await kbheDevice.connect(device.path);
        if (currentGeneration !== generation) {
          return;
        }

        _setDeviceInfo(device);

        if (device.kind === "updater") {
          _setFirmwareVersion(null);
          _setStatus("updater");
          DeviceSessionManager.startPresencePolling(currentGeneration);
          return;
        }

        try {
          const rawVersion = await kbheDevice.getFirmwareVersion();
          if (currentGeneration === generation) {
            _setFirmwareVersion(rawVersion);
          }
        } catch {
          if (currentGeneration === generation) {
            _setFirmwareVersion(null);
          }
        }

        if (currentGeneration !== generation) {
          return;
        }

        _setStatus("connected");
        DeviceSessionManager.startPresencePolling(currentGeneration);
      } catch (error) {
        if (currentGeneration !== generation) {
          return;
        }

        const message = error instanceof Error ? error.message : String(error);
        _setStatus("error");
        _setError(message);
        scheduleReconnect(3000);
      }
    })().finally(() => {
      connectPromise = null;
    });

    return connectPromise;
  },

  async disconnect() {
    generation += 1;
    clearReconnectTimer();
    stopPresencePolling();

    try {
      await kbheDevice.disconnect();
    } catch {
      // Ignore disconnect errors during teardown/reconnect.
    }

    resetDisconnectedState();
  },

  async reconnect() {
    await DeviceSessionManager.disconnect();
    await DeviceSessionManager.connect();
  },

  startPresencePolling(expectedGeneration: number) {
    stopPresencePolling();

    presenceInterval = window.setInterval(async () => {
      if (expectedGeneration !== generation) {
        stopPresencePolling();
        return;
      }

      const state = useDeviceSession.getState();
      if (state.status !== "connected" && state.status !== "updater") {
        stopPresencePolling();
        return;
      }

      try {
        const devices = await kbheDevice.listDevices();
        if (expectedGeneration !== generation) {
          return;
        }

        const connectedPath = useDeviceSession.getState().deviceInfo?.path;
        const stillPresent = connectedPath
          ? devices.some((device) => device.path === connectedPath)
          : devices.length > 0;

        if (!stillPresent) {
          stopPresencePolling();
          resetDisconnectedState();
          scheduleReconnect(1500);
        }
      } catch {
        if (expectedGeneration !== generation) {
          return;
        }
        stopPresencePolling();
        resetDisconnectedState();
        scheduleReconnect(1500);
      }
    }, 1500);
  },
};
