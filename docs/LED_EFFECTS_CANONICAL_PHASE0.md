# LED Effects Canonical Table (Phase 0)

This file freezes the historical effect IDs, symbols, and labels from before the phase-1B rebrand/renumber work.
It is intentionally pre-rebrand (including QMK symbol names) and kept only as an audit baseline during the breaking migration.

## Scope

- Firmware enum source: `Core/Inc/led_matrix.h`
- Host enum source: `kbhe_tool/protocol.py`
- Protocol IDs are currently aligned with the enum values below.

## Canonical IDs (Current Baseline)

| ID | Firmware Symbol | Current Label |
|---:|---|---|
| 0 | LED_EFFECT_NONE | Matrix (Software) |
| 1 | LED_EFFECT_RAINBOW | Rainbow Wave |
| 2 | LED_EFFECT_BREATHING | Breathing |
| 3 | LED_EFFECT_STATIC_RAINBOW | Static Rainbow |
| 4 | LED_EFFECT_SOLID | Solid Color |
| 5 | LED_EFFECT_PLASMA | Plasma |
| 6 | LED_EFFECT_FIRE | Fire |
| 7 | LED_EFFECT_OCEAN | Ocean Waves |
| 8 | LED_EFFECT_MATRIX | Matrix Rain |
| 9 | LED_EFFECT_SPARKLE | Sparkle |
| 10 | LED_EFFECT_BREATHING_RAINBOW | Breathing Rainbow |
| 11 | LED_EFFECT_SPIRAL | Spiral |
| 12 | LED_EFFECT_COLOR_CYCLE | Color Cycle |
| 13 | LED_EFFECT_REACTIVE | Reactive (Wave) |
| 14 | LED_EFFECT_THIRD_PARTY | Third-Party Live |
| 15 | LED_EFFECT_DISTANCE_SENSOR | Sensor Distance |
| 16 | LED_EFFECT_RAINBOW_DOME | Rainbow Dome |
| 17 | LED_EFFECT_SPHERE | Sphere |
| 18 | LED_EFFECT_DUAL_SPHERE | Dual Sphere |
| 19 | LED_EFFECT_STRIP_SPIN_ZOOM | Strip Spin + Zoom |
| 20 | LED_EFFECT_IMPACT_RAINBOW | Impact Rainbow |
| 21 | LED_EFFECT_REACTIVE_HEATMAP | Reactive Heatmap |
| 22 | LED_EFFECT_REACTIVE_GHOST | Reactive Ghost |
| 23 | LED_EFFECT_AUDIO_SPECTRUM | Audio Spectrum |
| 24 | LED_EFFECT_KEY_STATE_DEMO | Key State Demo |
| 25 | LED_EFFECT_QMK_CYCLE_PINWHEEL | QMK Cycle Pinwheel |
| 26 | LED_EFFECT_QMK_CYCLE_SPIRAL | QMK Cycle Spiral |
| 27 | LED_EFFECT_QMK_CYCLE_OUT_IN_DUAL | QMK Cycle Out-In Dual |
| 28 | LED_EFFECT_QMK_RAINBOW_BEACON | QMK Rainbow Beacon |
| 29 | LED_EFFECT_QMK_RAINBOW_PINWHEELS | QMK Rainbow Pinwheels |
| 30 | LED_EFFECT_QMK_RAINBOW_MOVING_CHEVRON | QMK Rainbow Moving Chevron |
| 31 | LED_EFFECT_QMK_HUE_BREATHING | QMK Hue Breathing |
| 32 | LED_EFFECT_QMK_HUE_PENDULUM | QMK Hue Pendulum |
| 33 | LED_EFFECT_QMK_HUE_WAVE | QMK Hue Wave |
| 34 | LED_EFFECT_QMK_RIVERFLOW | QMK Riverflow |
| 35 | LED_EFFECT_QMK_SOLID_COLOR | QMK Solid Color |
| 36 | LED_EFFECT_QMK_ALPHA_MODS | QMK Alpha Mods |
| 37 | LED_EFFECT_QMK_GRADIENT_UP_DOWN | QMK Gradient Up Down |
| 38 | LED_EFFECT_QMK_GRADIENT_LEFT_RIGHT | QMK Gradient Left Right |
| 39 | LED_EFFECT_QMK_BREATHING | QMK Breathing |
| 40 | LED_EFFECT_QMK_COLORBAND_SAT | QMK Colorband Sat |
| 41 | LED_EFFECT_QMK_COLORBAND_VAL | QMK Colorband Val |
| 42 | LED_EFFECT_QMK_COLORBAND_PINWHEEL_SAT | QMK Colorband Pinwheel Sat |
| 43 | LED_EFFECT_QMK_COLORBAND_PINWHEEL_VAL | QMK Colorband Pinwheel Val |
| 44 | LED_EFFECT_QMK_COLORBAND_SPIRAL_SAT | QMK Colorband Spiral Sat |
| 45 | LED_EFFECT_QMK_COLORBAND_SPIRAL_VAL | QMK Colorband Spiral Val |
| 46 | LED_EFFECT_QMK_CYCLE_ALL | QMK Cycle All |
| 47 | LED_EFFECT_QMK_CYCLE_LEFT_RIGHT | QMK Cycle Left Right |
| 48 | LED_EFFECT_QMK_CYCLE_UP_DOWN | QMK Cycle Up Down |
| 49 | LED_EFFECT_QMK_CYCLE_OUT_IN | QMK Cycle Out In |
| 50 | LED_EFFECT_QMK_DUAL_BEACON | QMK Dual Beacon |
| 51 | LED_EFFECT_QMK_FLOWER_BLOOMING | QMK Flower Blooming |
| 52 | LED_EFFECT_QMK_RAINDROPS | QMK Raindrops |
| 53 | LED_EFFECT_QMK_JELLYBEAN_RAINDROPS | QMK Jellybean Raindrops |
| 54 | LED_EFFECT_QMK_PIXEL_RAIN | QMK Pixel Rain |
| 55 | LED_EFFECT_QMK_PIXEL_FLOW | QMK Pixel Flow |
| 56 | LED_EFFECT_QMK_PIXEL_FRACTAL | QMK Pixel Fractal |
| 57 | LED_EFFECT_QMK_TYPING_HEATMAP | QMK Typing Heatmap |
| 58 | LED_EFFECT_QMK_DIGITAL_RAIN | QMK Digital Rain |
| 59 | LED_EFFECT_QMK_SOLID_REACTIVE_SIMPLE | QMK Solid Reactive Simple |
| 60 | LED_EFFECT_QMK_SOLID_REACTIVE | QMK Solid Reactive |
| 61 | LED_EFFECT_QMK_SOLID_REACTIVE_WIDE | QMK Solid Reactive Wide |
| 62 | LED_EFFECT_QMK_SOLID_REACTIVE_CROSS | QMK Solid Reactive Cross |
| 63 | LED_EFFECT_QMK_SOLID_REACTIVE_NEXUS | QMK Solid Reactive Nexus |
| 64 | LED_EFFECT_QMK_SPLASH | QMK Splash |
| 65 | LED_EFFECT_QMK_SOLID_SPLASH | QMK Solid Splash |
| 66 | LED_EFFECT_QMK_STARLIGHT_SMOOTH | QMK Starlight Smooth |
| 67 | LED_EFFECT_QMK_STARLIGHT | QMK Starlight |
| 68 | LED_EFFECT_QMK_STARLIGHT_DUAL_SAT | QMK Starlight Dual Sat |
| 69 | LED_EFFECT_QMK_STARLIGHT_DUAL_HUE | QMK Starlight Dual Hue |
| 70 | LED_EFFECT_QMK_SOLID_REACTIVE_MULTI_WIDE | QMK Solid Reactive Multi Wide |
| 71 | LED_EFFECT_QMK_SOLID_REACTIVE_MULTI_CROSS | QMK Solid Reactive Multi Cross |
| 72 | LED_EFFECT_QMK_SOLID_REACTIVE_MULTI_NEXUS | QMK Solid Reactive Multi Nexus |
| 73 | LED_EFFECT_QMK_MULTI_SPLASH | QMK Multi Splash |
| 74 | LED_EFFECT_QMK_SOLID_MULTI_SPLASH | QMK Solid Multi Splash |

## Notes

- This canonical table is intentionally pre-rebrand and pre-renumber.
- It exists to make future renames/ID compaction auditable and reviewable.
- Once phase-1B renumbering lands, a new canonical table should replace this file.
