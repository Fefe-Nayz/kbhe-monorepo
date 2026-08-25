/**
 * DeviceSessionManager — lightweight frontend session manager over KBHE transport.
 *
 * Goals:
 *  - never block the first render with long USB waits
 *  - avoid long-running Tauri invoke calls during normal runtime
 *  - keep connection state reactive and reconnect automatically
 */

import { create } from "zustand";
import { getVersion } from "@tauri-apps/api/app";
import { kbheDevice } from "./device";
import { kbheCommander } from "./commander";
import {
  kbheDeviceStorageId,
  kbheTransport,
  selectKbheSessionDevice,
  type KbheTransportDeviceInfo,
} from "./transport";
import {
  COMPATIBILITY_INTRODUCED_APP_VERSION,
  evaluateDeviceCompatibility,
  runtimeSessionStatus,
  type DeviceCompatibility,
} from "./compatibility";
import { startVolumeService, stopVolumeService } from "./volume-service";
import { SETTINGS_PROFILE_COUNT } from "./protocol";
import { setKbheQueryScope } from "@/lib/query/keys";
import {
  cancelAllThrottledCalls,
  cancelAndDrainAllThrottledCalls,
} from "@/hooks/use-throttled-call";

export type DeviceSessionStatus =
  | "disconnected"
  | "connecting"
  | "connected"
  | "updater"
  | "recovery-only"
  | "error";

export interface DeviceSessionState {
  status: DeviceSessionStatus;
  deviceInfo: KbheTransportDeviceInfo | null;
  firmwareVersion: string | null;
  compatibility: DeviceCompatibility | null;
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
  _setCompatibility: (compatibility: DeviceCompatibility | null) => void;
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
  compatibility: null,
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
  _setCompatibility: (compatibility) => set({ compatibility }),
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
let preferredSerialNumber: string | null = null;
let appVersionPromise: Promise<string> | null = null;

const CONNECTED_PRESENCE_POLL_MS = 3000;
const CONNECTED_PRESENCE_POLL_HIDDEN_MS = 6000;
const UPDATER_PRESENCE_POLL_MS = 2000;
const UPDATER_PRESENCE_POLL_HIDDEN_MS = 4000;
const PRESENCE_FAILURE_TOLERANCE = 2;

function isDocumentVisible(): boolean {
  return typeof document === "undefined" || document.visibilityState === "visible";
}

function presencePollDelay(status: DeviceSessionStatus): number {
  if (status === "updater" || status === "recovery-only") {
    return isDocumentVisible()
      ? UPDATER_PRESENCE_POLL_MS
      : UPDATER_PRESENCE_POLL_HIDDEN_MS;
  }

  return isDocumentVisible()
    ? CONNECTED_PRESENCE_POLL_MS
    : CONNECTED_PRESENCE_POLL_HIDDEN_MS;
}

function currentAppVersion(): Promise<string> {
  appVersionPromise ??= getVersion().catch(() => COMPATIBILITY_INTRODUCED_APP_VERSION);
  return appVersionPromise;
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
    _setCompatibility,
    _setError,
    _clearRuntimeProfileState,
  } = useDeviceSession.getState();
  _setStatus("disconnected");
  _setDeviceInfo(null);
  _setFirmwareVersion(null);
  _setCompatibility(null);
  _setError(null);
  _clearRuntimeProfileState();
  setKbheQueryScope(null, null);
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
      setKbheQueryScope(
        state.deviceInfo ? kbheDeviceStorageId(state.deviceInfo) : null,
        active?.profile_index ?? null,
      );
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

  async connect(expectedSerialNumber?: string) {
    const state = useDeviceSession.getState();
    if (state.status === "connecting" || connectPromise) {
      return connectPromise ?? Promise.resolve();
    }

    const currentGeneration = ++generation;
    connectPromise = (async () => {
      const {
        _setStatus,
        _setDeviceInfo,
        _setFirmwareVersion,
        _setCompatibility,
        _setError,
      } = useDeviceSession.getState();
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

        const requestedSerial = expectedSerialNumber?.trim() || preferredSerialNumber;
        const device = selectKbheSessionDevice(devices, requestedSerial);
        if (!device) {
          resetDisconnectedState();
          scheduleReconnect();
          return;
        }
        const serialNumber = device.serialNumber!.trim();

        await kbheDevice.connect(device.path, undefined, serialNumber);
        if (currentGeneration !== generation) {
          // disconnect() may have won the race before the HID open completed.
          // Do not leave an untracked backend handle connected in that case.
          try {
            await kbheDevice.disconnect();
          } catch {
            // The superseding session owns subsequent recovery.
          }
          return;
        }

        preferredSerialNumber = serialNumber;
        _setDeviceInfo(device);
        setKbheQueryScope(kbheDeviceStorageId(device), null);

        if (device.kind === "updater") {
          _setFirmwareVersion(null);
          let updaterProtocol: number | null = null;
          try {
            updaterProtocol = (await kbheTransport.getUpdaterInfo()).protocolVersion;
          } catch {
            // An updater that cannot complete the read-only HELLO handshake is
            // kept available for recovery, but never treated as compatible.
          }
          const compatibility = evaluateDeviceCompatibility({
            appVersion: await currentAppVersion(),
            updaterProtocol,
          });
          _setCompatibility(compatibility);
          _setStatus(compatibility.status === "compatible" ? "updater" : "recovery-only");
          useDeviceSession.getState()._clearRuntimeProfileState();
          DeviceSessionManager.startPresencePolling(currentGeneration);
          return;
        }

        let rawVersion: string | null = null;
        try {
          rawVersion = await kbheDevice.getFirmwareVersion();
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

        const compatibility = evaluateDeviceCompatibility({
          appVersion: await currentAppVersion(),
          firmwareVersion: rawVersion,
        });
        if (currentGeneration !== generation) {
          return;
        }
        _setCompatibility(compatibility);
        const runtimeStatus = runtimeSessionStatus(compatibility);
        _setStatus(runtimeStatus);
        if (runtimeStatus === "recovery-only") {
          useDeviceSession.getState()._clearRuntimeProfileState();
          DeviceSessionManager.startPresencePolling(currentGeneration);
          return;
        }
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
    const supersededConnect = connectPromise;
    cancelAllThrottledCalls();
    preferredSerialNumber = null;
    generation += 1;
    clearReconnectTimer();
    stopPresencePolling();
    stopVolumeService();

    try {
      await cancelAndDrainAllThrottledCalls();
      await kbheCommander.waitForIdle();
      await kbheDevice.disconnect();
    } catch {
      // Ignore disconnect errors during teardown/reconnect.
    }

    // A connect that was already enumerating/opening may finish after the
    // first backend disconnect. Wait for its generation-guarded cleanup so a
    // following reconnect cannot inherit (or be closed by) that stale open.
    if (supersededConnect) {
      try {
        await supersededConnect;
      } catch {
        // The disconnected state below is authoritative.
      }
    }

    resetDisconnectedState();
  },

  async reconnect() {
    const serialNumber = useDeviceSession.getState().deviceInfo?.serialNumber?.trim()
      || preferredSerialNumber
      || undefined;
    await DeviceSessionManager.disconnect();
    await DeviceSessionManager.connect(serialNumber);
  },

  async reenumerate(timeoutSeconds = 8): Promise<boolean> {
    const supersededConnect = connectPromise;
    const currentGeneration = ++generation;
    clearReconnectTimer();
    stopPresencePolling();
    stopVolumeService();
    cancelAllThrottledCalls();

    const state = useDeviceSession.getState();
    const expectedSerialNumber = state.deviceInfo?.serialNumber?.trim()
      || preferredSerialNumber;
    if (!expectedSerialNumber) {
      state._setStatus("error");
      state._setError("The connected keyboard has no stable USB serial number");
      return false;
    }
    state._setStatus("connecting");
    state._setError(null);

    try {
      if (supersededConnect) {
        await supersededConnect;
      }
      await cancelAndDrainAllThrottledCalls();
      await kbheCommander.waitForIdle();
      const ok = await kbheDevice.usbReenumerate(timeoutSeconds);
      if (!ok || currentGeneration !== generation) {
        if (!ok && currentGeneration === generation) {
          throw new Error("The keyboard did not reconnect after USB re-enumeration");
        }
        return false;
      }

      const [connection, devices] = await Promise.all([
        kbheDevice.connectionState(),
        kbheDevice.listDevices(),
      ]);
      if (currentGeneration !== generation) {
        return false;
      }

      const device = devices.find(
        (candidate) => candidate.path === connection.path
          && candidate.serialNumber?.trim() === expectedSerialNumber,
      );
      if (!device || device.kind !== "runtime") {
        throw new Error("The re-enumerated keyboard could not be identified");
      }

      preferredSerialNumber = expectedSerialNumber;
      state._setDeviceInfo(device);
      setKbheQueryScope(kbheDeviceStorageId(device), null);
      state._setStatus("connected");
      await DeviceSessionManager.refreshRuntimeProfileState(currentGeneration);
      DeviceSessionManager.startPresencePolling(currentGeneration);
      void DeviceSessionManager.syncVolumeService();
      return true;
    } catch (error) {
      if (currentGeneration !== generation) {
        return false;
      }

      const message = error instanceof Error ? error.message : String(error);
      state._setStatus("error");
      state._setError(message);
      scheduleReconnect(1500);
      return false;
    }
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
      if (
        state.status !== "connected"
        && state.status !== "updater"
        && state.status !== "recovery-only"
      ) {
        stopPresencePolling();
        return;
      }

      try {
        const connectedPath = state.deviceInfo?.path;
        const connectedSerialNumber = state.deviceInfo?.serialNumber?.trim()
          || preferredSerialNumber;
        const connectedKind = state.deviceInfo?.kind;
        const matchesConnectedIdentity = (device: KbheTransportDeviceInfo) => Boolean(
          connectedSerialNumber
          && device.serialNumber?.trim() === connectedSerialNumber
          && (!connectedKind || device.kind === connectedKind),
        );
        let stillPresent = false;

        if (state.status === "connected") {
          stillPresent = await kbheDevice.ping();

          // Fallback to enumeration only when ping misses to avoid expensive scans every tick.
          if (!stillPresent) {
            const devices = await kbheDevice.listDevices();
            if (expectedGeneration !== generation) {
              return;
            }
            stillPresent = connectedSerialNumber
              ? devices.some(matchesConnectedIdentity)
              : Boolean(connectedPath && devices.some((device) => device.path === connectedPath));
          }
        } else {
          const devices = await kbheDevice.listDevices();
          if (expectedGeneration !== generation) {
            return;
          }
          stillPresent = connectedSerialNumber
            ? devices.some(matchesConnectedIdentity)
            : Boolean(connectedPath && devices.some((device) => device.path === connectedPath));
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
        if (state.status === "connected") {
          const active = await kbheDevice.getActiveProfile();
          if (expectedGeneration !== generation) {
            return;
          }
          if (active && active.profile_index !== useDeviceSession.getState().activeProfileIndex) {
            useDeviceSession.getState()._setRuntimeProfileState({
              activeProfileIndex: active.profile_index,
              profileUsedMask: active.profile_used_mask,
              lastRuntimeSyncAt: Date.now(),
            });
            setKbheQueryScope(
              state.deviceInfo ? kbheDeviceStorageId(state.deviceInfo) : null,
              active.profile_index,
            );
          }
        }
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
      if (
        latestStatus !== "connected"
        && latestStatus !== "updater"
        && latestStatus !== "recovery-only"
      ) {
        stopPresencePolling();
        return;
      }

      scheduleNextPoll(presencePollDelay(latestStatus));
    };

    scheduleNextPoll(presencePollDelay(useDeviceSession.getState().status));
  },
};
