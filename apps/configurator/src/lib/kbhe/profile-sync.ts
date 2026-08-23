import {
  type FilterParams,
  type GamepadSettings,
  type KeyGamepadMap,
  type KeySettings,
  kbheDevice,
  type LedIdleOptions,
  type RotaryEncoderSettings,
  type TriggerChatterGuard,
} from "./device"
import {
  GAMEPAD_CURVE_MAX_DISTANCE_MM,
  KEY_COUNT,
  LAYER_COUNT,
  LED_EFFECT_COUNT,
  LED_EFFECT_PARAM_COUNT,
} from "./protocol"
import { z } from "zod"
import {
  ACTION_OVERLAY_COUNT,
  ACTION_PROGRAM_COUNT,
  actionOverlayBindingSchema,
  actionProgramSchema,
  defaultActionProgram,
  findActionProgramCycle,
  findActionProgramDepthOverflow,
  type ActionCapabilities,
  type ActionOverlayBinding,
  type ActionProgram,
} from "./action-program"
import {
  isProfileOperationToken,
  runProfileOperation,
  type ProfileOperationToken,
} from "./profile-operation-lock"

export interface FirmwareLedSnapshot {
  enabled: boolean | null
  brightness: number | null
  pixels: number[] | null
  effectMode: number | null
  fpsLimit: number | null
  effectParams: number[][] | null
  idleOptions: LedIdleOptions | null
  triggerChatterGuard: TriggerChatterGuard | null
}

export interface FirmwareProfileSnapshot {
  schemaVersion: 1 | 2
  capturedAt: number
  sourceProfileIndex: number
  profileId?: string
  revision?: number
  deviceId?: string | null
  capabilities?: string[]
  actionPrograms?: ActionProgram[] | null
  actionProgramNames?: string[]
  actionOverlays?: ActionOverlayBinding[] | null
  actionOverlayNames?: string[]
  actionStateBits?: number | null
  keySettings: KeySettings[]
  keyGamepadMaps?: KeyGamepadMap[] | null
  gamepadSettings: GamepadSettings | null
  rotarySettings: RotaryEncoderSettings | null
  filterEnabled: boolean | null
  filterParams: FilterParams | null
  options: {
    keyboard_enabled: boolean
    gamepad_enabled: boolean
    raw_hid_echo: boolean
    led_thermal_protection_enabled?: boolean
  } | null
  nkroEnabled: boolean | null
  advancedTickRate: number | null
  led?: FirmwareLedSnapshot
}

interface ApplyFirmwareProfileSnapshotOptions {
  persistToFlash?: boolean
  restoreActiveProfile?: boolean
  operationToken?: ProfileOperationToken
}

const byteSchema = z.number().int().min(0).max(0xff)
const u16Schema = z.number().int().min(0).max(0xffff)
const finiteSchema = z.number().finite()
const dynamicZoneSchema = z.object({
  end_mm_tenths: byteSchema,
  // Legacy configurator snapshots stored a fabricated distance here. It is
  // accepted for migration only; end_mm_tenths is the DKS action bitmap.
  end_mm: finiteSchema.min(0).max(25.5).optional(),
  hid_keycode: u16Schema,
}).strict()
const keySettingsSchema = z.object({
  key_index: z.number().int().min(0).max(KEY_COUNT - 1),
  profile_index: z.number().int().min(0).max(3),
  layer_index: z.number().int().min(0).max(LAYER_COUNT - 1),
  hid_keycode: u16Schema,
  actuation_point_mm: finiteSchema.min(0).max(25.5),
  release_point_mm: finiteSchema.min(0).max(25.5),
  rapid_trigger_press: finiteSchema.min(0).max(2.55),
  rapid_trigger_release: finiteSchema.min(0).max(2.55),
  socd_pair: z.number().int().min(0).max(KEY_COUNT - 1).nullable(),
  socd_resolution: byteSchema,
  rapid_trigger_enabled: z.boolean(),
  continuous_rapid_trigger: z.boolean(),
  behavior_mode: byteSchema,
  hold_threshold_ms: u16Schema,
  secondary_hid_keycode: u16Schema,
  dynamic_zones: z.array(dynamicZoneSchema).max(4),
  tap_hold_options: byteSchema,
  dks_bottom_out_point_mm: finiteSchema.min(0).max(25.5),
  socd_fully_pressed_enabled: z.boolean(),
  socd_fully_pressed_point_mm: finiteSchema.min(0).max(25.5),
  disable_kb_on_gamepad: z.boolean(),
}).strict()
const gamepadPointSchema = z.object({
  x_01mm: z.number().int().min(0).max(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100),
  x_mm: finiteSchema.min(0).max(GAMEPAD_CURVE_MAX_DISTANCE_MM),
  y: byteSchema,
}).strict()
const gamepadSettingsSchema = z.object({
  deadzone: byteSchema,
  keyboard_routing: z.number().int().min(0).max(2),
  square_mode: z.boolean(),
  reactive_stick: z.boolean(),
  api_mode: z.number().int().min(0).max(1),
  curve_points: z.array(gamepadPointSchema).length(4),
}).strict()
const rotaryBindingSchema = z.object({
  mode: z.number().int().min(0).max(1),
  keycode: u16Schema,
  modifier_mask_exact: byteSchema,
  fallback_no_mod_keycode: u16Schema,
  layer_mode: z.number().int().min(0).max(1),
  layer_index: z.number().int().min(0).max(LAYER_COUNT - 1),
}).strict()
const rotarySettingsSchema = z.object({
  rotation_action: z.number().int().min(0).max(4),
  button_action: z.number().int().min(0).max(4),
  sensitivity: z.number().int().min(1).max(16),
  acceleration: byteSchema.max(3).default(0),
  step_size: z.number().int().min(1).max(64),
  invert_direction: z.boolean(),
  rgb_behavior: z.number().int().min(0).max(3),
  rgb_effect_mode: z.number().int().min(0).max(LED_EFFECT_COUNT - 1).refine((value) => value !== 7),
  progress_style: z.number().int().min(0).max(2),
  progress_effect_mode: z.number().int().min(0).max(LED_EFFECT_COUNT - 1).refine((value) => value !== 7),
  progress_color: z.tuple([byteSchema, byteSchema, byteSchema]),
  progress_filled_only: z.boolean().default(false),
  cw_binding: rotaryBindingSchema,
  ccw_binding: rotaryBindingSchema,
  click_binding: rotaryBindingSchema,
}).strict()
const ledIdleOptionsSchema = z.object({
  idle_timeout_seconds: byteSchema,
  allow_system_when_disabled: z.boolean(),
  third_party_stream_counts_as_activity: z.boolean(),
  usb_suspend_rgb_off: z.boolean(),
}).strict()
const chatterSchema = z.object({ enabled: z.boolean(), duration_ms: byteSchema }).strict()
const ledSnapshotSchema = z.object({
  enabled: z.boolean().nullable(),
  brightness: byteSchema.nullable(),
  pixels: z.array(byteSchema).length(KEY_COUNT * 3).nullable(),
  effectMode: z.number().int().min(0).max(LED_EFFECT_COUNT - 1).nullable(),
  fpsLimit: byteSchema.nullable(),
  effectParams: z.array(z.array(byteSchema).length(LED_EFFECT_PARAM_COUNT)).length(LED_EFFECT_COUNT).nullable(),
  idleOptions: ledIdleOptionsSchema.nullable(),
  triggerChatterGuard: chatterSchema.nullable(),
}).strict()

export const firmwareProfileSnapshotSchema = z.object({
  schemaVersion: z.union([z.literal(1), z.literal(2)]),
  capturedAt: z.number().int().nonnegative(),
  sourceProfileIndex: z.number().int().min(0).max(3),
  profileId: z.string().min(1).max(128).optional(),
  revision: z.number().int().nonnegative().optional(),
  deviceId: z.string().max(256).nullable().optional(),
  capabilities: z.array(z.string().min(1).max(64)).max(64).optional(),
  actionPrograms: z.array(actionProgramSchema).length(ACTION_PROGRAM_COUNT).nullable().optional(),
  actionProgramNames: z.array(z.string().max(64)).length(ACTION_PROGRAM_COUNT).optional(),
  actionOverlays: z.array(actionOverlayBindingSchema).length(ACTION_OVERLAY_COUNT).nullable().optional(),
  actionOverlayNames: z.array(z.string().max(64)).length(ACTION_OVERLAY_COUNT).optional(),
  actionStateBits: u16Schema.nullable().optional(),
  keySettings: z.array(keySettingsSchema).min(1).max(KEY_COUNT * LAYER_COUNT),
  keyGamepadMaps: z.array(z.object({
    key_index: z.number().int().min(0).max(KEY_COUNT - 1),
    axis: z.number().int().min(0).max(6),
    direction: z.number().int().min(0).max(1),
    button: z.number().int().min(0).max(17),
    layer_mask: z.number().int().min(1).max((1 << LAYER_COUNT) - 1),
  }).strict()).max(KEY_COUNT).nullable().optional(),
  gamepadSettings: gamepadSettingsSchema.nullable(),
  rotarySettings: rotarySettingsSchema.nullable(),
  filterEnabled: z.boolean().nullable(),
  filterParams: z.object({
    noise_band: u16Schema,
    alpha_min_denom: byteSchema,
    alpha_max_denom: byteSchema,
  }).strict().nullable(),
  options: z.object({
    keyboard_enabled: z.boolean(),
    gamepad_enabled: z.boolean(),
    raw_hid_echo: z.boolean(),
    led_thermal_protection_enabled: z.boolean().optional(),
  }).strict().nullable(),
  nkroEnabled: z.boolean().nullable(),
  advancedTickRate: byteSchema.nullable(),
  led: ledSnapshotSchema.optional(),
}).strict().superRefine((snapshot, context) => {
  if (snapshot.actionPrograms) {
    const cycle = findActionProgramCycle(snapshot.actionPrograms)
    if (cycle) {
      context.addIssue({
        code: "custom",
        path: ["actionPrograms", cycle[0] ?? 0],
        message: `Recursive macro cycle: ${cycle.map((slot) => `Macro ${slot + 1}`).join(" → ")}`,
      })
    }
    /* Nesting capacity is device-specific and is validated against the live
     * GET_ACTION_CAPABILITIES.maxInstances value immediately before apply. */
  }
  const keyLocations = new Set<string>()
  snapshot.keySettings.forEach((settings, index) => {
    const location = `${settings.layer_index}:${settings.key_index}`
    if (keyLocations.has(location)) {
      context.addIssue({
        code: "custom",
        path: ["keySettings", index],
        message: "Duplicate key/layer settings entry",
      })
    }
    keyLocations.add(location)
  })

  if (snapshot.keyGamepadMaps) {
    const mappedKeys = new Set<number>()
    snapshot.keyGamepadMaps.forEach((mapping, index) => {
      if (mappedKeys.has(mapping.key_index)) {
        context.addIssue({
          code: "custom",
          path: ["keyGamepadMaps", index],
          message: "Duplicate gamepad mapping entry",
        })
      }
      mappedKeys.add(mapping.key_index)
    })
  }

  const points = snapshot.gamepadSettings?.curve_points
  if (points) {
    for (let index = 0; index < points.length; index += 1) {
      const point = points[index]
      if (index > 0 && point.x_01mm < points[index - 1].x_01mm) {
        context.addIssue({
          code: "custom",
          path: ["gamepadSettings", "curve_points", index],
          message: "Gamepad curve points must be ordered",
        })
      }
      if (Math.abs(point.x_mm - point.x_01mm / 100) > 0.005) {
        context.addIssue({
          code: "custom",
          path: ["gamepadSettings", "curve_points", index, "x_mm"],
          message: "Gamepad curve distance fields disagree",
        })
      }
    }
  }
})

export function parseFirmwareProfileSnapshot(value: unknown): FirmwareProfileSnapshot | null {
  const parsed = firmwareProfileSnapshotSchema.safeParse(value)
  return parsed.success ? parsed.data as FirmwareProfileSnapshot : null
}

export function isFirmwareProfileSnapshot(value: unknown): value is FirmwareProfileSnapshot {
  return firmwareProfileSnapshotSchema.safeParse(value).success
}

async function captureAllKeySettings(profileIndex: number): Promise<KeySettings[] | null> {
  const keySettings: KeySettings[] = []
  const batchSize = 8

  for (let layerIndex = 0; layerIndex < LAYER_COUNT; layerIndex += 1) {
    for (let start = 0; start < KEY_COUNT; start += batchSize) {
      const end = Math.min(start + batchSize, KEY_COUNT)
      const requests = Array.from({ length: end - start }, (_, offset) =>
        kbheDevice.getKeySettings(start + offset, profileIndex, layerIndex),
      )

      const results = await Promise.all(requests)
      for (const result of results) {
        if (!result) {
          return null
        }
        keySettings.push(result)
      }
    }
  }

  return keySettings
}

async function captureAllKeyGamepadMaps(): Promise<KeyGamepadMap[] | null> {
  const maps: KeyGamepadMap[] = []
  const batchSize = 8

  for (let start = 0; start < KEY_COUNT; start += batchSize) {
    const end = Math.min(start + batchSize, KEY_COUNT)
    const results = await Promise.all(
      Array.from({ length: end - start }, (_, offset) =>
        kbheDevice.getKeyGamepadMap(start + offset),
      ),
    )

    for (const result of results) {
      if (!result) {
        return null
      }
      maps.push(result)
    }
  }

  return maps
}

async function captureLedSnapshot(): Promise<FirmwareLedSnapshot> {
  const [
    enabled,
    brightness,
    pixels,
    effectMode,
    fpsLimit,
    idleOptions,
    triggerChatterGuard,
  ] = await Promise.all([
    kbheDevice.ledGetEnabled(),
    kbheDevice.ledGetBrightness(),
    kbheDevice.ledDownloadAll(),
    kbheDevice.getLedEffect(),
    kbheDevice.getLedFpsLimit(),
    kbheDevice.getLedIdleOptions(),
    kbheDevice.getTriggerChatterGuard(),
  ])

  const effectParamResults = await Promise.all(
    Array.from({ length: LED_EFFECT_COUNT }, (_, effect) =>
      kbheDevice.getLedEffectParams(effect),
    ),
  )

  const effectParams = effectParamResults.every((params) => params != null)
    ? effectParamResults.map((params) => {
        const values = Array.from(params ?? [], (value) => value & 0xff)
          .slice(0, LED_EFFECT_PARAM_COUNT)
        while (values.length < LED_EFFECT_PARAM_COUNT) {
          values.push(0)
        }
        return values
      })
    : null

  return {
    enabled,
    brightness,
    pixels,
    effectMode,
    fpsLimit,
    effectParams,
    idleOptions,
    triggerChatterGuard,
  }
}

async function captureActionSnapshot(profileIndex: number): Promise<Pick<
  FirmwareProfileSnapshot,
  "capabilities" | "actionPrograms" | "actionProgramNames" | "actionOverlays" | "actionOverlayNames" | "actionStateBits"
>> {
  const capabilities = await kbheDevice.getActionCapabilities()
  if (!capabilities) {
    return {
      capabilities: [],
      actionPrograms: null,
      actionProgramNames: Array.from({ length: ACTION_PROGRAM_COUNT }, (_, index) => `Macro ${index + 1}`),
      actionOverlays: null,
      actionOverlayNames: Array.from({ length: ACTION_OVERLAY_COUNT }, (_, index) => `Overlay ${index + 1}`),
      actionStateBits: null,
    }
  }

  const [programResults, overlayResults, states] = await Promise.all([
    Promise.all(Array.from({ length: capabilities.programCount }, (_, index) => (
      kbheDevice.getActionProgram(profileIndex, index)
    ))),
    Promise.all(Array.from({ length: capabilities.overlayCount }, (_, index) => (
      kbheDevice.getActionOverlay(profileIndex, index)
    ))),
    kbheDevice.getActionStates(),
  ])

  return {
    capabilities: ["action-programs-v1", "state-overlays-v1"],
    actionPrograms: programResults.length === ACTION_PROGRAM_COUNT && programResults.every(Boolean)
      ? programResults as ActionProgram[]
      : null,
    actionProgramNames: Array.from({ length: ACTION_PROGRAM_COUNT }, (_, index) => `Macro ${index + 1}`),
    actionOverlays: overlayResults.length === ACTION_OVERLAY_COUNT && overlayResults.every(Boolean)
      ? overlayResults as ActionOverlayBinding[]
      : null,
    actionOverlayNames: Array.from({ length: ACTION_OVERLAY_COUNT }, (_, index) => `Overlay ${index + 1}`),
    actionStateBits: states?.bits ?? null,
  }
}

function createProfileDocumentId(): string {
  return globalThis.crypto?.randomUUID?.()
    ?? `profile-${Date.now()}-${Math.random().toString(16).slice(2)}`
}

async function captureFirmwareProfileSnapshotUnlocked(profileIndex: number): Promise<FirmwareProfileSnapshot | null> {
  let restoreProfileIndex: number | null = null
  let switchedProfile = false

  try {
    const [activeProfile, ramOnlyMode] = await Promise.all([
      kbheDevice.getActiveProfile(),
      kbheDevice.getRamOnlyMode(),
    ])

    if (activeProfile?.profile_index != null && activeProfile.profile_index !== profileIndex) {
      if (ramOnlyMode) {
        return null
      }

      restoreProfileIndex = activeProfile.profile_index
      const switched = await kbheDevice.setActiveProfile(profileIndex)
      if (!switched) {
        return null
      }
      switchedProfile = true
    }

    const keySettings = await captureAllKeySettings(profileIndex)
    if (!keySettings) {
      return null
    }

    const [
      gamepadSettings,
      rotarySettings,
      filterEnabled,
      filterParams,
      options,
      nkroEnabled,
      advancedTickRate,
      led,
      keyGamepadMaps,
      actions,
      deviceInfo,
    ] = await Promise.all([
      kbheDevice.getGamepadSettings(),
      kbheDevice.getRotaryEncoderSettings(),
      kbheDevice.getFilterEnabled(),
      kbheDevice.getFilterParams(),
      kbheDevice.getOptions(),
      kbheDevice.getNkroEnabled(),
      kbheDevice.getAdvancedTickRate(),
      captureLedSnapshot(),
      captureAllKeyGamepadMaps(),
      captureActionSnapshot(profileIndex),
      kbheDevice.getDeviceInfo(),
    ])

    return {
      schemaVersion: 2,
      capturedAt: Date.now(),
      sourceProfileIndex: profileIndex,
      profileId: createProfileDocumentId(),
      revision: 1,
      deviceId: deviceInfo?.serial_number ?? null,
      ...actions,
      keySettings,
      keyGamepadMaps,
      gamepadSettings,
      rotarySettings,
      filterEnabled,
      filterParams,
      options,
      nkroEnabled,
      advancedTickRate,
      led,
    }
  } catch {
    return null
  } finally {
    if (switchedProfile && restoreProfileIndex != null) {
      await kbheDevice.setActiveProfile(restoreProfileIndex)
    }
  }
}

export async function captureFirmwareProfileSnapshot(
  profileIndex: number,
  operationToken?: ProfileOperationToken,
): Promise<FirmwareProfileSnapshot | null> {
  if (isProfileOperationToken(operationToken)) {
    return captureFirmwareProfileSnapshotUnlocked(profileIndex)
  }
  return runProfileOperation(() => captureFirmwareProfileSnapshotUnlocked(profileIndex))
}

async function applyLedSnapshot(led: FirmwareLedSnapshot): Promise<boolean> {
  if (led.effectParams) {
    for (let effect = 0; effect < led.effectParams.length; effect += 1) {
      const params = led.effectParams[effect]
      if (!params) {
        continue
      }
      const ok = await kbheDevice.setLedEffectParams(effect, params)
      if (!ok) {
        return false
      }
    }
  }

  if (led.pixels) {
    const ok = await kbheDevice.ledUploadAll(led.pixels)
    if (!ok) {
      return false
    }
  }

  if (led.brightness != null) {
    const ok = await kbheDevice.ledSetBrightness(led.brightness)
    if (!ok) {
      return false
    }
  }

  if (led.fpsLimit != null) {
    const ok = await kbheDevice.setLedFpsLimit(led.fpsLimit)
    if (!ok) {
      return false
    }
  }

  if (led.idleOptions) {
    const ok = await kbheDevice.setLedIdleOptions(
      led.idleOptions.idle_timeout_seconds,
      led.idleOptions.allow_system_when_disabled,
      led.idleOptions.third_party_stream_counts_as_activity,
      led.idleOptions.usb_suspend_rgb_off,
    )
    if (!ok) {
      return false
    }
  }

  if (led.triggerChatterGuard) {
    const ok = await kbheDevice.setTriggerChatterGuard(
      led.triggerChatterGuard.enabled,
      led.triggerChatterGuard.duration_ms,
    )
    if (!ok) {
      return false
    }
  }

  if (led.enabled != null) {
    const ok = await kbheDevice.ledSetEnabled(led.enabled)
    if (!ok) {
      return false
    }
  }

  if (led.effectMode != null) {
    const ok = await kbheDevice.setLedEffect(led.effectMode)
    if (!ok) {
      return false
    }
  }

  return true
}

function actionProgramsFitDevice(
  programs: readonly ActionProgram[],
  capabilities: ActionCapabilities | null,
): boolean {
  return capabilities != null
    && capabilities.programCount >= programs.length
    && capabilities.maxInstances >= 1
    && findActionProgramDepthOverflow(programs, capabilities.maxInstances) == null
}

export async function applyFirmwareProfileSnapshot(
  snapshot: FirmwareProfileSnapshot,
  targetProfileIndex: number,
  options: ApplyFirmwareProfileSnapshotOptions = {},
): Promise<boolean> {
  if (!isProfileOperationToken(options.operationToken)) {
    return runProfileOperation((operationToken) => applyFirmwareProfileSnapshot(snapshot, targetProfileIndex, {
      ...options,
      operationToken,
    }))
  }

  const validatedSnapshot = parseFirmwareProfileSnapshot(snapshot)
  if (!validatedSnapshot) {
    return false
  }
  snapshot = validatedSnapshot

  if (snapshot.actionPrograms) {
    const actionCapabilities = await kbheDevice.getActionCapabilities()
    if (!actionProgramsFitDevice(snapshot.actionPrograms, actionCapabilities)) {
      return false
    }
  }

  let restoreProfileIndex: number | null = null
  let switchedProfile = false
  const shouldRestore = options.restoreActiveProfile ?? true
  let restoreNeedsSave = false
  let stagedPersistentDocument = false
  let expectedDocumentGeneration = 0

  try {
    if (options.persistToFlash) {
      const ramOnly = await kbheDevice.getRamOnlyMode()
      if (ramOnly) {
        const exited = await kbheDevice.exitRamOnlyMode()
        if (!exited) {
          return false
        }
      }
    }

    const activeProfile = await kbheDevice.getActiveProfile()
    if (activeProfile?.profile_index != null && activeProfile.profile_index !== targetProfileIndex) {
      restoreProfileIndex = activeProfile.profile_index
      const switched = await kbheDevice.setActiveProfile(targetProfileIndex)
      if (!switched) {
        return false
      }
      switchedProfile = true
      restoreNeedsSave = Boolean(options.persistToFlash)
    }

    if (options.persistToFlash) {
      const documentMeta = await kbheDevice.getProfileDocumentMeta(targetProfileIndex)
      if (!documentMeta) {
        return false
      }
      expectedDocumentGeneration = documentMeta.generation
      const entered = await kbheDevice.enterRamOnlyMode()
      if (!entered) {
        return false
      }
      stagedPersistentDocument = true
    }

    const orderedKeySettings = [...snapshot.keySettings].sort((a, b) => {
      if (a.layer_index !== b.layer_index) {
        return a.layer_index - b.layer_index
      }
      return a.key_index - b.key_index
    })

    for (const settings of orderedKeySettings) {
      const ok = await kbheDevice.setKeySettingsExtended(settings.key_index, {
        ...settings,
        profile_index: targetProfileIndex,
        layer_index: settings.layer_index,
      })
      if (!ok) {
        return false
      }
    }

    if (snapshot.keyGamepadMaps) {
      for (const map of snapshot.keyGamepadMaps) {
        const ok = await kbheDevice.setKeyGamepadMap(
          map.key_index,
          map.axis,
          map.direction,
          map.button,
          map.layer_mask,
        )
        if (!ok) {
          return false
        }
      }
    }

    if (snapshot.gamepadSettings) {
      const ok = await kbheDevice.setGamepadSettings(snapshot.gamepadSettings)
      if (!ok) {
        return false
      }
    }

    if (snapshot.rotarySettings) {
      const ok = await kbheDevice.setRotaryEncoderSettings(snapshot.rotarySettings)
      if (!ok) {
        return false
      }
    }

    if (snapshot.filterEnabled != null) {
      const ok = await kbheDevice.setFilterEnabled(snapshot.filterEnabled)
      if (!ok) {
        return false
      }
    }

    if (snapshot.filterParams) {
      const ok = await kbheDevice.setFilterParams(
        snapshot.filterParams.noise_band,
        snapshot.filterParams.alpha_min_denom,
        snapshot.filterParams.alpha_max_denom,
      )
      if (!ok) {
        return false
      }
    }

    // Device output routing is global rather than part of settings_profile_t.
    // During a persistent ProfileDocument transaction it must be applied only
    // after the durable document commit and the O(1) RAM-only leave, then
    // saved globally.
    if (snapshot.options && !options.persistToFlash) {
      const optionsOk = await kbheDevice.setOptions({
        keyboard_enabled: snapshot.options.keyboard_enabled,
        gamepad_enabled: snapshot.options.gamepad_enabled,
        raw_hid_echo: snapshot.options.raw_hid_echo,
        led_thermal_protection_enabled:
          snapshot.options.led_thermal_protection_enabled ?? true,
      })
      if (!optionsOk) {
        return false
      }
    }

    if (snapshot.nkroEnabled != null && !options.persistToFlash) {
      const ok = await kbheDevice.setNkroEnabled(snapshot.nkroEnabled)
      if (!ok) {
        return false
      }
    }

    if (snapshot.advancedTickRate != null) {
      const ok = await kbheDevice.setAdvancedTickRate(snapshot.advancedTickRate)
      if (!ok) {
        return false
      }
    }

    if (snapshot.led) {
      const ok = await applyLedSnapshot(snapshot.led)
      if (!ok) {
        return false
      }
    }

    if (snapshot.actionPrograms) {
      /* Remove every existing edge before installing the target DAG. Source
       * and target can both be acyclic while a direct slot-by-slot migration
       * creates a transient cycle (for example M2→M1 to M1→M2). */
      for (let index = 0; index < ACTION_PROGRAM_COUNT; index += 1) {
        const cleared = await kbheDevice.setActionProgram(
          targetProfileIndex,
          index,
          defaultActionProgram(),
          false,
        )
        if (!cleared) {
          return false
        }
      }
      for (let index = 0; index < snapshot.actionPrograms.length; index += 1) {
        const ok = await kbheDevice.setActionProgram(
          targetProfileIndex,
          index,
          snapshot.actionPrograms[index],
          false,
        )
        if (!ok) {
          return false
        }
      }
    }

    if (snapshot.actionOverlays) {
      for (let index = 0; index < snapshot.actionOverlays.length; index += 1) {
        const ok = await kbheDevice.setActionOverlay(
          targetProfileIndex,
          index,
          snapshot.actionOverlays[index],
          false,
        )
        if (!ok) {
          return false
        }
      }
    }

    if (snapshot.actionStateBits != null) {
      for (let stateIndex = 0; stateIndex < 16; stateIndex += 1) {
        const ok = await kbheDevice.setActionState(
          stateIndex,
          Boolean(snapshot.actionStateBits & (1 << stateIndex)),
        )
        if (!ok) {
          return false
        }
      }
    }

    if (options.persistToFlash) {
      const committed = await kbheDevice.commitProfileDocument(
        targetProfileIndex,
        expectedDocumentGeneration,
      )
      if (!committed) {
        return false
      }
      const leftRamOnly = await kbheDevice.leaveRamOnlyMode()
      if (!leftRamOnly) {
        return false
      }
      stagedPersistentDocument = false

      if (snapshot.options) {
        const optionsOk = await kbheDevice.setOptions({
          keyboard_enabled: snapshot.options.keyboard_enabled,
          gamepad_enabled: snapshot.options.gamepad_enabled,
          raw_hid_echo: snapshot.options.raw_hid_echo,
          led_thermal_protection_enabled:
            snapshot.options.led_thermal_protection_enabled ?? true,
        })
        if (!optionsOk) {
          return false
        }
      }

      if (snapshot.nkroEnabled != null) {
        const nkroOk = await kbheDevice.setNkroEnabled(snapshot.nkroEnabled)
        if (!nkroOk) {
          return false
        }
      }
    }

    if (switchedProfile && shouldRestore && restoreProfileIndex != null) {
      const restored = await kbheDevice.setActiveProfile(restoreProfileIndex)
      if (!restored) {
        return false
      }
      switchedProfile = false
    }

    if (options.persistToFlash) {
      // Persists global options/NKRO and whichever active-profile selection the
      // caller requested. The profile body itself was already committed by CAS.
      const saved = await kbheDevice.saveSettings()
      if (!saved) {
        return false
      }
      restoreNeedsSave = false
    }

    return true
  } catch {
    return false
  } finally {
    if (stagedPersistentDocument) {
      await kbheDevice.exitRamOnlyMode()
    }
    if (switchedProfile && shouldRestore && restoreProfileIndex != null) {
      const restored = await kbheDevice.setActiveProfile(restoreProfileIndex)
      if (restored && restoreNeedsSave) {
        await kbheDevice.saveSettings()
      }
    }
  }
}
