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
    filterEnabled: () => ["device", "filterEnabled"] as const,
    filterParams: () => ["device", "filterParams"] as const,
    mcuMetrics: () => ["device", "mcuMetrics"] as const,
    lockStates: () => ["device", "lockStates"] as const,
  },

  keymap: {
    allSettings: () => ["keymap", "allSettings"] as const,
    keySettings: (index: number) => ["keymap", "keySettings", index] as const,
    layerKeycode: (layer: number, key: number) => ["keymap", "layerKeycode", layer, key] as const,
    allLayerKeycodes: (layer: number) => ["keymap", "layerKeycodes", layer] as const,
  },

  gamepad: {
    settings: () => ["gamepad", "settings"] as const,
    keyMap: (index: number) => ["gamepad", "keyMap", index] as const,
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
