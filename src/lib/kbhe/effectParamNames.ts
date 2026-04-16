import { LEDEffect } from "./protocol";

/**
 * Software-side human-readable names for per-effect tuning parameters.
 * Keyed by [LEDEffect][paramId].  Only custom params (0–7) need entries here;
 * param 8–10 (Color), 11–13 (Color 2), and 14 (Speed) are labelled by the
 * renderer from the type field alone.
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
    0: "Hue Step",
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
    6: "Saturation",
    7: "Wave Drift",
  },
  [LEDEffect.REACTIVE_GHOST]: {
    0: "Decay",
    1: "Spread",
    2: "Trail",
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
    1: "Pressed Brightness",
    2: "Released Brightness",
  },
  [LEDEffect.SOLID_COLOR]: {
    0: "Brightness",
  },
  [LEDEffect.BREATHING]: {
    0: "Brightness Floor",
    1: "Brightness Ceiling",
    2: "Plateau",
  },
  [LEDEffect.GRADIENT_LEFT_RIGHT]: {
    0: "Horizontal Scale",
    1: "Vertical Scale",
    2: "Saturation",
    3: "Value",
  },
  [LEDEffect.GRADIENT_UP_DOWN]: {
    0: "Vertical Hue Spread",
    1: "Saturation",
    2: "Value",
  },
  [LEDEffect.CYCLE_LEFT_RIGHT]: {
    0: "Horizontal Hue Spread",
    1: "Vertical Hue Contribution",
    3: "Saturation",
    4: "Gradient Tilt",
    5: "Sinusoidal Warp",
  },
  [LEDEffect.CYCLE_UP_DOWN]: {
    0: "Vertical Hue Spread",
    1: "Horizontal Hue Contribution",
    3: "Saturation",
    4: "Gradient Tilt",
    5: "Sinusoidal Warp",
  },
  [LEDEffect.DIGITAL_RAIN]: {
    0: "Trail Length",
    1: "Head Size",
    2: "Density",
    3: "White Heads",
    4: "Hue Bias",
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
  },
  [LEDEffect.MULTI_SPLASH]: {
    0: "Lifetime",
    1: "Radius",
    2: "Base Glow",
    3: "White Core",
    4: "Gain",
    5: "Palette",
    6: "Mode",
  },
  [LEDEffect.STARLIGHT_DUAL_SAT]: {
    0: "Saturation Spread",
  },
  [LEDEffect.STARLIGHT_DUAL_HUE]: {
    0: "Hue Spread",
  },
  [LEDEffect.HUE_BREATHING]: {
    0: "Hue Swing Range",
  },
  [LEDEffect.HUE_PENDULUM]: {
    0: "Hue Swing Range",
  },
  [LEDEffect.HUE_WAVE]: {
    0: "Wave Amplitude",
  },
  [LEDEffect.ALPHA_MODS]: {
    0: "Hue Offset",
  },
};
