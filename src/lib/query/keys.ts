/**
 * Centralised TanStack Query key factory.
 * Keeps invalidation explicit and co-located with keys.
 */

export const queryKeys = {
  device: {
    identity: () => ["device", "identity"] as const,
    options: () => ["device", "options"] as const,
    keyboardEnabled: () => ["device", "keyboardEnabled"] as const,
    gamepadEnabled: () => ["device", "gamepadEnabled"] as const,
    nkroEnabled: () => ["device", "nkroEnabled"] as const,
    advancedTickRate: () => ["device", "advancedTickRate"] as const,
    triggerChatterGuard: () => ["device", "triggerChatterGuard"] as const,
    filterEnabled: () => ["device", "filterEnabled"] as const,
    filterParams: () => ["device", "filterParams"] as const,
    mcuMetrics: () => ["device", "mcuMetrics"] as const,
    lockStates: () => ["device", "lockStates"] as const,
  },

  profile: {
    active: () => ["profile", "active"] as const,
    default: () => ["profile", "default"] as const,
    names: () => ["profile", "names"] as const,
    usedMask: () => ["profile", "usedMask"] as const,
    ramOnly: () => ["profile", "ramOnly"] as const,
  },

  keymap: {
    allSettings: (layer: number, profileIndex: number, runtimeSource: "device" | "app") =>
      ["keymap", "allSettings", layer, profileIndex, runtimeSource] as const,
    keySettings: (
      index: number,
      layer: number,
      profileIndex: number,
      runtimeSource: "device" | "app",
    ) => ["keymap", "keySettings", index, layer, profileIndex, runtimeSource] as const,
    layerKeycode: (layer: number, key: number, profileIndex: number) =>
      ["keymap", "layerKeycode", layer, key, profileIndex] as const,
    allLayerKeycodes: (layer: number, profileIndex: number) =>
      ["keymap", "layerKeycodes", layer, profileIndex] as const,
  },

  gamepad: {
    settings: () => ["gamepad", "settings"] as const,
    keyMap: (index: number, layer: number) => ["gamepad", "keyMap", index, layer] as const,
    allKeyMaps: (layer: number) => ["gamepad", "keyMap", "all", layer] as const,
    withKeyboard: () => ["gamepad", "withKeyboard"] as const,
  },

  calibration: {
    all: () => ["calibration", "all"] as const,
    guidedStatus: () => ["calibration", "guidedStatus"] as const,
    keyCurve: (index: number) => ["calibration", "keyCurve", index] as const,
  },

  led: {
    enabled: () => ["led", "enabled"] as const,
    brightness: () => ["led", "brightness"] as const,
    effect: () => ["led", "effect"] as const,
    ledEnabled: () => ["device", "ledEnabled"] as const,
    effectSpeed: () => ["led", "effectSpeed"] as const,
    effectColor: () => ["led", "effectColor"] as const,
    effectParams: (mode: number) => ["led", "effectParams", mode] as const,
    fpsLimit: () => ["led", "fpsLimit"] as const,
    idleOptions: () => ["led", "idleOptions"] as const,
    diagnostic: () => ["led", "diagnostic"] as const,
    allPixels: () => ["led", "allPixels"] as const,
  },

  rotary: {
    settings: () => ["rotary", "settings"] as const,
  },

  diagnostics: {
    adcValues: () => ["diagnostics", "adcValues"] as const,
    keyStates: () => ["diagnostics", "keyStates"] as const,
  },
};
