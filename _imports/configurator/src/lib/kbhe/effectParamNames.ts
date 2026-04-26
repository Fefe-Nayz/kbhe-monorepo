import { LEDEffect } from "./protocol";

/**
 * Software-side human-readable names for per-effect tuning parameters.
 * Keyed by [LEDEffect][paramId].  Most custom params (0–7, and optional 15)
 * should be named here; param 8–10 (Color), 11–13 (Color 2), and 14 (Speed)
 * are usually labelled by the renderer from the type field alone.
 *
 * Names mirror the comments in Core/Src/led_matrix.c.
 */
export const EFFECT_PARAM_NAMES: Partial<Record<number, Record<number, string>>> = {
  [LEDEffect.PLASMA]: {
    0: "Motion Depth",
    1: "Saturation",
    2: "Radial Warp",
    3: "Value",
  },
  [LEDEffect.FIRE]: {
    0: "Heat Boost",
    1: "Ember Floor",
    2: "Cooling",
    3: "Palette",
  },
  [LEDEffect.OCEAN]: {
    0: "Hue Bias",
    1: "Depth Dimming",
    2: "Foam Highlight",
    3: "Crest Speed",
    4: "Wave Angle",
  },
  [LEDEffect.SPARKLE]: {
    0: "Density",
    1: "Sparkle Brightness",
    2: "Rainbow Mix",
    3: "Ambient Glow",
  },
  [LEDEffect.BREATHING_RAINBOW]: {
    0: "Brightness Floor",
    1: "Hue Drift",
    2: "Saturation",
  },
  [LEDEffect.COLOR_CYCLE]: {
    0: "Cycle Multiplier",
    1: "Saturation",
    2: "Value",
    3: "Effect-Color Mix",
  },
  [LEDEffect.DISTANCE_SENSOR]: {
    0: "Brightness Floor",
    1: "Hue Span",
    2: "Saturation",
    3: "Reverse Gradient",
  },
  [LEDEffect.IMPACT_RAINBOW]: {
    0: "Boost Mode",
    1: "Boost Decay",
    2: "Key Boost",
    3: "Audio Boost",
    4: "Max Boost",
    5: "Angle",
    6: "Saturation Scale",
    7: "Wave Drift",
    12: "Audio Sensitivity",
    15: "Pattern Mode",
  },
  [LEDEffect.REACTIVE_GHOST]: {
    0: "Decay",
    1: "Spread",
    2: "Trail Hold",
    3: "Gain",
  },
  [LEDEffect.AUDIO_SPECTRUM]: {
    0: "Hue Span",
    1: "Base Floor",
    2: "Peak Gain",
    3: "Mirror",
    4: "Decay",
    5: "Visualizer Mode",
    6: "Contrast",
  },
  [LEDEffect.KEY_STATE_DEMO]: {
    0: "Invert Mapping",
  },
  [LEDEffect.SOLID_COLOR]: {},
  [LEDEffect.BREATHING]: {
    0: "Brightness Floor",
    1: "Brightness Ceiling",
    2: "Plateau",
  },
  [LEDEffect.GRADIENT_LEFT_RIGHT]: {
    0: "Horizontal Hue Spread",
    1: "Gradient Axis Angle",
  },
  [LEDEffect.GRADIENT_UP_DOWN]: {
    0: "Vertical Hue Spread",
    1: "Gradient Axis Angle",
  },
  [LEDEffect.CYCLE_LEFT_RIGHT]: {
    0: "Axis Hue Spread",
    1: "Axis Angle",
  },
  [LEDEffect.CYCLE_UP_DOWN]: {
    0: "Axis Hue Spread",
    1: "Axis Angle",
  },
  [LEDEffect.CYCLE_OUT_IN]: {
    0: "Radial Spread",
    1: "Center X",
    2: "Center Y",
    3: "Reverse Direction",
  },
  [LEDEffect.CYCLE_OUT_IN_DUAL]: {
    0: "Radial Spread",
    1: "Center 1 X",
    2: "Center 1 Y",
    3: "Center 2 X",
    4: "Center 2 Y",
    5: "Reverse Direction 1",
    6: "Reverse Direction 2",
  },
  [LEDEffect.CYCLE_SPIRAL]: {
    0: "Tightness",
    1: "Swirl Strength",
    2: "Center X",
    3: "Center Y",
    4: "Reverse Direction",
  },
  [LEDEffect.CYCLE_PINWHEEL]: {
    0: "Center X",
    1: "Center Y",
    2: "Reverse Direction",
  },
  [LEDEffect.DUAL_BEACON]: {
    0: "Center X",
    1: "Center Y",
    2: "Reverse Direction",
  },
  [LEDEffect.RAINBOW_BEACON]: {
    0: "Center X",
    1: "Center Y",
    2: "Reverse Direction",
  },
  [LEDEffect.RAINBOW_PINWHEELS]: {
    0: "Center 1 X",
    1: "Center 1 Y",
    2: "Center 2 X",
    3: "Center 2 Y",
    4: "Reverse Direction 1",
    5: "Reverse Direction 2",
  },
  [LEDEffect.RAINBOW_MOVING_CHEVRON]: {
    0: "Chevron Size",
    1: "Axis Angle",
    2: "Reverse Motion",
  },
  [LEDEffect.RIVERFLOW]: {
    0: "Flow Angle",
  },
  [LEDEffect.COLORBAND_SAT]: {
    0: "Axis Angle",
    1: "Use Value Channel",
  },
  [LEDEffect.COLORBAND_VAL]: {
    0: "Axis Angle",
    1: "Use Value Channel",
  },
  [LEDEffect.COLORBAND_PINWHEEL_SAT]: {
    0: "Center X",
    1: "Center Y",
    2: "Reverse Direction",
    3: "Use Value Channel",
  },
  [LEDEffect.COLORBAND_PINWHEEL_VAL]: {
    0: "Center X",
    1: "Center Y",
    2: "Reverse Direction",
    3: "Use Value Channel",
  },
  [LEDEffect.COLORBAND_SPIRAL_SAT]: {
    0: "Tightness",
    1: "Swirl Strength",
    2: "Center X",
    3: "Center Y",
    4: "Reverse Direction",
    5: "Use Value Channel",
  },
  [LEDEffect.COLORBAND_SPIRAL_VAL]: {
    0: "Tightness",
    1: "Swirl Strength",
    2: "Center X",
    3: "Center Y",
    4: "Reverse Direction",
    5: "Use Value Channel",
  },
  [LEDEffect.RAINDROPS]: {
    0: "Hue Range",
  },
  [LEDEffect.JELLYBEAN_RAINDROPS]: {
    0: "Hue Range",
  },
  [LEDEffect.PIXEL_RAIN]: {
    0: "Hue Range",
  },
  [LEDEffect.PIXEL_FLOW]: {
    0: "Hue Range",
    1: "Reverse Direction",
    2: "Flow Angle",
  },
  [LEDEffect.DIGITAL_RAIN]: {
    0: "Trail Length",
    1: "Head Size",
    2: "Density",
    3: "White Heads",
    4: "Hue Bias",
    5: "Flow Angle",
  },
  [LEDEffect.SOLID_REACTIVE_WIDE]: {
    0: "Decay",
    1: "Spread",
    2: "Gain",
    3: "Multi Hit",
  },
  [LEDEffect.SOLID_REACTIVE_MULTI_WIDE]: {
    0: "Decay",
    1: "Spread",
    2: "Gain",
    3: "Multi Hit",
  },
  [LEDEffect.SOLID_REACTIVE_CROSS]: {
    0: "Decay",
    1: "Spread",
    2: "Gain",
    3: "Multi Hit",
  },
  [LEDEffect.SOLID_REACTIVE_MULTI_CROSS]: {
    0: "Decay",
    1: "Spread",
    2: "Gain",
    3: "Multi Hit",
  },
  [LEDEffect.SOLID_REACTIVE_NEXUS]: {
    0: "Decay",
    1: "Spread",
    2: "Gain",
    3: "Multi Hit",
  },
  [LEDEffect.SOLID_REACTIVE_MULTI_NEXUS]: {
    0: "Decay",
    1: "Spread",
    2: "Gain",
    3: "Multi Hit",
  },
  [LEDEffect.TYPING_HEATMAP]: {
    0: "Heat Gain",
    1: "Decay",
    2: "Diffusion",
    3: "Floor",
  },
  [LEDEffect.SPLASH]: {
    0: "Lifetime",
    1: "Radius",
    2: "Base Glow",
    3: "White Core",
    4: "Gain",
    5: "Palette",
    6: "Mode",
    7: "Random Color Per Click",
    15: "Multi Hit",
  },
  [LEDEffect.MULTI_SPLASH]: {
    0: "Lifetime",
    1: "Radius",
    2: "Base Glow",
    3: "White Core",
    4: "Gain",
    5: "Palette",
    6: "Mode",
    7: "Random Color Per Click",
    15: "Multi Hit",
  },
  [LEDEffect.SOLID_SPLASH]: {
    0: "Random Color Per Click",
    1: "Multi Hit",
  },
  [LEDEffect.SOLID_MULTI_SPLASH]: {
    0: "Random Color Per Click",
    1: "Multi Hit",
  },
  [LEDEffect.BASS_RIPPLE]: {
    0: "Lifetime",
    1: "Radius",
    2: "Base Glow",
    3: "White Core",
    4: "Gain",
    5: "Palette",
    6: "Bass Sensitivity",
    7: "Random Color Per Beat",
  },
  [LEDEffect.STARLIGHT_DUAL_SAT]: {
    0: "Spread",
    1: "Hue Mode",
  },
  [LEDEffect.STARLIGHT_DUAL_HUE]: {
    0: "Spread",
    1: "Hue Mode",
  },
  [LEDEffect.HUE_BREATHING]: {
    0: "Hue Swing Range",
  },
  [LEDEffect.HUE_PENDULUM]: {
    0: "Hue Swing Range",
    1: "Axis Angle",
  },
  [LEDEffect.HUE_WAVE]: {
    0: "Wave Amplitude",
    1: "Axis Angle",
  },
  [LEDEffect.ALPHA_MODS]: {
    0: "Hue Offset",
    1: "Non-Alpha Saturation",
    2: "Non-Alpha Brightness",
  },
};

export interface EffectParamEnumOption {
  value: number;
  label: string;
}

export const EFFECT_PARAM_ENUM_OPTIONS: Partial<Record<number, Record<number, EffectParamEnumOption[]>>> = {
  [LEDEffect.FIRE]: {
    3: [
      { value: 0, label: "Fire" },
      { value: 1, label: "Amber" },
      { value: 2, label: "Ice" },
    ],
  },
  [LEDEffect.IMPACT_RAINBOW]: {
    0: [
      { value: 0, label: "Speed Boost" },
      { value: 1, label: "White Flash" },
      { value: 2, label: "Both" },
    ],
    15: [
      { value: 0, label: "Wave" },
      { value: 1, label: "Pinwheel" },
      { value: 2, label: "Spiral" },
      { value: 3, label: "Cycle Out-In" },
    ],
  },
  [LEDEffect.AUDIO_SPECTRUM]: {
    5: [
      { value: 0, label: "Bars" },
      { value: 1, label: "Center Bands" },
      { value: 2, label: "Side Sweep" },
      { value: 3, label: "Focused Bars" },
    ],
  },
  [LEDEffect.SPLASH]: {
    5: [
      { value: 0, label: "Custom Color" },
      { value: 1, label: "Rainbow" },
      { value: 2, label: "QMK Classic Wave" },
    ],
    6: [
      { value: 0, label: "QMK Style" },
      { value: 1, label: "Physics Ring + Trail" },
    ],
  },
  [LEDEffect.MULTI_SPLASH]: {
    5: [
      { value: 0, label: "Custom Color" },
      { value: 1, label: "Rainbow" },
      { value: 2, label: "QMK Classic Wave" },
    ],
    6: [
      { value: 0, label: "QMK Style" },
      { value: 1, label: "Physics Ring + Trail" },
    ],
  },
  [LEDEffect.BASS_RIPPLE]: {
    5: [
      { value: 0, label: "Custom Color" },
      { value: 1, label: "Rainbow" },
    ],
  },
  [LEDEffect.COLORBAND_SAT]: {
    1: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Value" },
    ],
  },
  [LEDEffect.COLORBAND_VAL]: {
    1: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Value" },
    ],
  },
  [LEDEffect.COLORBAND_PINWHEEL_SAT]: {
    3: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Value" },
    ],
  },
  [LEDEffect.COLORBAND_PINWHEEL_VAL]: {
    3: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Value" },
    ],
  },
  [LEDEffect.COLORBAND_SPIRAL_SAT]: {
    5: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Value" },
    ],
  },
  [LEDEffect.COLORBAND_SPIRAL_VAL]: {
    5: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Value" },
    ],
  },
  [LEDEffect.STARLIGHT_DUAL_SAT]: {
    1: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Hue" },
    ],
  },
  [LEDEffect.STARLIGHT_DUAL_HUE]: {
    1: [
      { value: 0, label: "Saturation" },
      { value: 1, label: "Hue" },
    ],
  },
};
