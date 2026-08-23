interface QueryScope {
  deviceId: string;
  profileIndex: number | null;
  epoch: number;
}

let scope: QueryScope = { deviceId: "disconnected", profileIndex: null, epoch: 0 };

export function setKbheQueryScope(deviceId: string | null, profileIndex: number | null): void {
  const nextDeviceId = deviceId ?? "disconnected";
  if (scope.deviceId === nextDeviceId && scope.profileIndex === profileIndex) {
    return;
  }
  scope = { deviceId: nextDeviceId, profileIndex, epoch: scope.epoch + 1 };
}

function deviceScope() {
  return [scope.deviceId, scope.epoch] as const;
}

function profileScope() {
  return [scope.deviceId, scope.profileIndex, scope.epoch] as const;
}

/**
 * Centralised TanStack Query key factory.
 * Keeps invalidation explicit and co-located with keys.
 */

export const queryKeys = {
  device: {
    identity: () => ["device", ...deviceScope(), "identity"] as const,
    options: () => ["device", ...profileScope(), "options"] as const,
    keyboardEnabled: () => ["device", ...profileScope(), "keyboardEnabled"] as const,
    gamepadEnabled: () => ["device", ...profileScope(), "gamepadEnabled"] as const,
    nkroEnabled: () => ["device", ...profileScope(), "nkroEnabled"] as const,
    advancedTickRate: () => ["device", ...profileScope(), "advancedTickRate"] as const,
    triggerChatterGuard: () => ["device", ...profileScope(), "triggerChatterGuard"] as const,
    filterEnabled: () => ["device", ...profileScope(), "filterEnabled"] as const,
    filterParams: () => ["device", ...profileScope(), "filterParams"] as const,
    mcuMetrics: () => ["device", ...deviceScope(), "mcuMetrics"] as const,
    lockStates: () => ["device", ...deviceScope(), "lockStates"] as const,
  },

  profile: {
    active: () => ["profile", ...deviceScope(), "active"] as const,
    default: () => ["profile", ...deviceScope(), "default"] as const,
    names: () => ["profile", ...deviceScope(), "names"] as const,
    usedMask: () => ["profile", ...deviceScope(), "usedMask"] as const,
    ramOnly: () => ["profile", ...profileScope(), "ramOnly"] as const,
  },

  keymap: {
    allSettings: (layer: number, profileIndex: number, runtimeSource: "device" | "app") =>
      ["keymap", ...deviceScope(), "allSettings", layer, profileIndex, runtimeSource] as const,
    keySettings: (
      index: number,
      layer: number,
      profileIndex: number,
      runtimeSource: "device" | "app",
    ) => ["keymap", ...deviceScope(), "keySettings", index, layer, profileIndex, runtimeSource] as const,
    layerKeycode: (layer: number, key: number, profileIndex: number) =>
      ["keymap", ...deviceScope(), "layerKeycode", layer, key, profileIndex] as const,
    allLayerKeycodes: (layer: number, profileIndex: number) =>
      ["keymap", ...deviceScope(), "layerKeycodes", layer, profileIndex] as const,
  },

  gamepad: {
    settings: () => ["gamepad", ...profileScope(), "settings"] as const,
    keyMap: (index: number, layer: number) => ["gamepad", ...profileScope(), "keyMap", index, layer] as const,
    allKeyMaps: (layer: number) => ["gamepad", ...profileScope(), "keyMap", "all", layer] as const,
    withKeyboard: () => ["gamepad", ...profileScope(), "withKeyboard"] as const,
  },

  calibration: {
    all: () => ["calibration", ...deviceScope(), "all"] as const,
    guidedStatus: () => ["calibration", ...deviceScope(), "guidedStatus"] as const,
    keyCurve: (index: number) => ["calibration", ...profileScope(), "keyCurve", index] as const,
  },

  led: {
    enabled: () => ["led", ...profileScope(), "enabled"] as const,
    brightness: () => ["led", ...profileScope(), "brightness"] as const,
    effect: () => ["led", ...profileScope(), "effect"] as const,
    ledEnabled: () => ["device", ...profileScope(), "ledEnabled"] as const,
    effectSpeed: () => ["led", ...profileScope(), "effectSpeed"] as const,
    effectColor: () => ["led", ...profileScope(), "effectColor"] as const,
    effectParams: (mode: number) => ["led", ...profileScope(), "effectParams", mode] as const,
    fpsLimit: () => ["led", ...profileScope(), "fpsLimit"] as const,
    idleOptions: () => ["led", ...profileScope(), "idleOptions"] as const,
    diagnostic: () => ["led", ...profileScope(), "diagnostic"] as const,
    allPixels: () => ["led", ...profileScope(), "allPixels"] as const,
  },

  rotary: {
    settings: () => ["rotary", ...profileScope(), "settings"] as const,
  },

  actions: {
    capabilities: () => ["actions", ...deviceScope(), "capabilities"] as const,
    program: (index: number) => ["actions", ...profileScope(), "program", index] as const,
    overlay: (index: number) => ["actions", ...profileScope(), "overlay", index] as const,
    states: () => ["actions", ...profileScope(), "states"] as const,
  },

  diagnostics: {
    adcValues: () => ["diagnostics", ...deviceScope(), "adcValues"] as const,
    keyStates: () => ["diagnostics", ...deviceScope(), "keyStates"] as const,
    allKeySettings: () => ["diagnostics", ...profileScope(), "allKeySettings"] as const,
  },
};
