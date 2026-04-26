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
import { startVolumeService, stopVolumeService } from "./volume-service";
import { SETTINGS_PROFILE_COUNT } from "./protocol";

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
  activeProfileIndex: number | null;
  defaultProfileIndex: number | null;
  profileUsedMask: number;
  profileNames: string[];
  ramOnlyMode: boolean | null;
  lastRuntimeSyncAt: number | null;

  _setStatus: (status: DeviceSessionStatus) => void;
  _setDeviceInfo: (info: KbheTransportDeviceInfo | null) => void;
  _setFirmwareVersion: (v: string | null) => void;
  _setError: (e: string | null) => void;
  _setRuntimeProfileState: (next: Partial<Pick<
    DeviceSessionState,
    "activeProfileIndex" | "defaultProfileIndex" | "profileUsedMask" | "profileNames" | "ramOnlyMode" | "lastRuntimeSyncAt"
  >>) => void;
  _clearRuntimeProfileState: () => void;
  setDeveloperMode: (enabled: boolean) => void;
}

const DEV_MODE_KEY = "kbhe-developer-mode";

export const useDeviceSession = create<DeviceSessionState>()((set) => ({
  status: "disconnected",
  deviceInfo: null,
  firmwareVersion: null,
  error: null,
  developerMode: localStorage.getItem(DEV_MODE_KEY) === "true",
  activeProfileIndex: null,
  defaultProfileIndex: null,
  profileUsedMask: 0,
  profileNames: [],
  ramOnlyMode: null,
  lastRuntimeSyncAt: null,

  _setStatus: (status) => set({ status }),
  _setDeviceInfo: (deviceInfo) => set({ deviceInfo }),
  _setFirmwareVersion: (firmwareVersion) => set({ firmwareVersion }),
  _setError: (error) => set({ error }),
  _setRuntimeProfileState: (next) => set(next),
  _clearRuntimeProfileState: () => set({
    activeProfileIndex: null,
    defaultProfileIndex: null,
    profileUsedMask: 0,
    profileNames: [],
    ramOnlyMode: null,
    lastRuntimeSyncAt: null,
  }),
  setDeveloperMode: (developerMode) => {
    localStorage.setItem(DEV_MODE_KEY, String(developerMode));
    set({ developerMode });
  },
}));

let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let presenceTimer: ReturnType<typeof setTimeout> | null = null;
let connectPromise: Promise<void> | null = null;
let initialized = false;
let generation = 0;
let presenceFailures = 0;

const CONNECTED_PRESENCE_POLL_MS = 3000;
const CONNECTED_PRESENCE_POLL_HIDDEN_MS = 6000;
const UPDATER_PRESENCE_POLL_MS = 2000;
const UPDATER_PRESENCE_POLL_HIDDEN_MS = 4000;
const PRESENCE_FAILURE_TOLERANCE = 2;

function isDocumentVisible(): boolean {
  return typeof document === "undefined" || document.visibilityState === "visible";
}

function presencePollDelay(status: DeviceSessionStatus): number {
  if (status === "updater") {
    return isDocumentVisible()
      ? UPDATER_PRESENCE_POLL_MS
      : UPDATER_PRESENCE_POLL_HIDDEN_MS;
  }

  return isDocumentVisible()
    ? CONNECTED_PRESENCE_POLL_MS
    : CONNECTED_PRESENCE_POLL_HIDDEN_MS;
}

function clearReconnectTimer() {
  if (reconnectTimer !== null) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
}

function stopPresencePolling() {
  if (presenceTimer !== null) {
    clearTimeout(presenceTimer);
    presenceTimer = null;
  }
  presenceFailures = 0;
}

function resetDisconnectedState() {
  const {
    _setStatus,
    _setDeviceInfo,
    _setFirmwareVersion,
    _setError,
    _clearRuntimeProfileState,
  } = useDeviceSession.getState();
  _setStatus("disconnected");
  _setDeviceInfo(null);
  _setFirmwareVersion(null);
  _setError(null);
  _clearRuntimeProfileState();
}

function scheduleReconnect(delayMs = 2500) {
  clearReconnectTimer();
  reconnectTimer = window.setTimeout(() => {
    void DeviceSessionManager.connect();
  }, delayMs);
}

export const DeviceSessionManager = {
  normalizeDefaultProfileIndex(profileIndex: number | null | undefined): number | null {
    if (profileIndex == null || profileIndex === 0xff) {
      return null;
    }
    return profileIndex;
  },

  async refreshRuntimeProfileState(expectedGeneration?: number) {
    if (expectedGeneration != null && expectedGeneration !== generation) {
      return;
    }

    const state = useDeviceSession.getState();
    if (state.status !== "connected") {
      return;
    }

    try {
      const [active, defaultProfile, ramOnlyMode] = await Promise.all([
        kbheDevice.getActiveProfile(),
        kbheDevice.getDefaultProfile(),
        kbheDevice.getRamOnlyMode(),
      ]);

      if (expectedGeneration != null && expectedGeneration !== generation) {
        return;
      }

      const nameResults = await Promise.all(
        Array.from({ length: SETTINGS_PROFILE_COUNT }, (_, slot) => kbheDevice.getProfileName(slot)),
      );

      if (expectedGeneration != null && expectedGeneration !== generation) {
        return;
      }

      useDeviceSession.getState()._setRuntimeProfileState({
        activeProfileIndex: active?.profile_index ?? null,
        defaultProfileIndex: DeviceSessionManager.normalizeDefaultProfileIndex(
          defaultProfile?.profile_index,
        ),
        profileUsedMask: active?.profile_used_mask ?? defaultProfile?.profile_used_mask ?? 0,
        profileNames: nameResults.map((item, index) => item?.name ?? `Slot ${index + 1}`),
        ramOnlyMode,
        lastRuntimeSyncAt: Date.now(),
      });
    } catch {
      if (expectedGeneration != null && expectedGeneration !== generation) {
        return;
      }

      useDeviceSession.getState()._setRuntimeProfileState({
        lastRuntimeSyncAt: Date.now(),
      });
    }
  },

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
          useDeviceSession.getState()._clearRuntimeProfileState();
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
        void DeviceSessionManager.refreshRuntimeProfileState(currentGeneration);
        DeviceSessionManager.startPresencePolling(currentGeneration);
        void DeviceSessionManager.syncVolumeService();
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
    stopVolumeService();

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

  async syncVolumeService() {
    try {
      const rotary = await kbheDevice.getRotaryEncoderSettings();
      if (rotary && rotary.rotation_action === 0) {
        startVolumeService();
      } else {
        stopVolumeService();
      }
    } catch {
      stopVolumeService();
    }
  },

  startPresencePolling(expectedGeneration: number) {
    stopPresencePolling();

    const scheduleNextPoll = (delayMs: number) => {
      presenceTimer = window.setTimeout(() => {
        void pollOnce();
      }, Math.max(0, delayMs));
    };

    const markDisconnected = () => {
      stopPresencePolling();
      stopVolumeService();
      resetDisconnectedState();
      scheduleReconnect(1500);
    };

    const pollOnce = async () => {
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
        const connectedPath = state.deviceInfo?.path;
        let stillPresent = false;

        if (state.status === "connected") {
          stillPresent = await kbheDevice.ping();

          // Fallback to enumeration only when ping misses to avoid expensive scans every tick.
          if (!stillPresent) {
            const devices = await kbheDevice.listDevices();
            if (expectedGeneration !== generation) {
              return;
            }
            stillPresent = connectedPath
              ? devices.some((device) => device.path === connectedPath)
              : devices.length > 0;
          }
        } else {
          const devices = await kbheDevice.listDevices();
          if (expectedGeneration !== generation) {
            return;
          }
          stillPresent = connectedPath
            ? devices.some((device) => device.path === connectedPath)
            : devices.length > 0;
        }

        if (expectedGeneration !== generation) {
          return;
        }

        if (!stillPresent) {
          presenceFailures += 1;
          if (presenceFailures >= PRESENCE_FAILURE_TOLERANCE) {
            markDisconnected();
            return;
          }
          scheduleNextPoll(500);
          return;
        }

        presenceFailures = 0;
      } catch {
        if (expectedGeneration !== generation) {
          return;
        }

        presenceFailures += 1;
        if (presenceFailures >= PRESENCE_FAILURE_TOLERANCE) {
          markDisconnected();
          return;
        }
        scheduleNextPoll(750);
        return;
      }

      const latestStatus = useDeviceSession.getState().status;
      if (latestStatus !== "connected" && latestStatus !== "updater") {
        stopPresencePolling();
        return;
      }

      scheduleNextPoll(presencePollDelay(latestStatus));
    };

    scheduleNextPoll(presencePollDelay(useDeviceSession.getState().status));
  },
};
