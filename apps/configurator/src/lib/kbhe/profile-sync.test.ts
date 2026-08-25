import { describe, expect, test } from "bun:test";

import { kbheDevice } from "./device";
import {
  applyFirmwareProfileSnapshot,
  firmwareProfileSnapshotSchema,
} from "./profile-sync";
import {
  ACTION_ENGINE_MAX_INSTANCES,
  ACTION_MACRO_KEYCODE_BASE,
  ACTION_PROGRAM_COUNT,
  ActionOpcode,
  defaultActionProgram,
  findActionProgramCycle,
  type ActionProgram,
} from "./action-program";

function validSnapshot() {
  return {
    schemaVersion: 2,
    capturedAt: 1,
    sourceProfileIndex: 0,
    profileId: "test-profile",
    revision: 1,
    deviceId: "serial:test",
    capabilities: [],
    keySettings: [{
      key_index: 0,
      profile_index: 0,
      layer_index: 0,
      hid_keycode: 4,
      actuation_point_mm: 1,
      release_point_mm: 1,
      rapid_trigger_press: 0.1,
      rapid_trigger_release: 0.1,
      socd_pair: null,
      socd_resolution: 0,
      rapid_trigger_enabled: false,
      continuous_rapid_trigger: false,
      behavior_mode: 0,
      hold_threshold_ms: 200,
      secondary_hid_keycode: 0,
      dynamic_zones: [],
      tap_hold_options: 0,
      dks_bottom_out_point_mm: 4,
      socd_fully_pressed_enabled: false,
      socd_fully_pressed_point_mm: 4,
      disable_kb_on_gamepad: false,
    }],
    gamepadSettings: null,
    rotarySettings: null,
    filterEnabled: null,
    filterParams: null,
    options: null,
    nkroEnabled: null,
    advancedTickRate: null,
  };
}

function actionCapabilities(maxInstances = ACTION_ENGINE_MAX_INSTANCES) {
  return {
    programVersion: 1,
    profileCount: 4,
    programCount: ACTION_PROGRAM_COUNT,
    maxSteps: 32,
    stateCount: 16,
    overlayCount: 8,
    stepSize: 4,
    maxInstances,
    profileDocumentSchemaVersion: 2,
    atomicProfileDocumentCommit: true,
    runtimeStateCommand: true,
    extendedStateReport: true,
  };
}

describe("ProfileDocument v2 validation", () => {
  test("accepts a bounded version-2 document", () => {
    expect(firmwareProfileSnapshotSchema.safeParse(validSnapshot()).success).toBeTrue();
  });

  test("rejects unknown top-level fields and invalid nested key indexes", () => {
    expect(firmwareProfileSnapshotSchema.safeParse({ ...validSnapshot(), unexpected: true }).success).toBeFalse();
    const invalid = validSnapshot();
    invalid.keySettings[0]!.key_index = 82;
    expect(firmwareProfileSnapshotSchema.safeParse(invalid).success).toBeFalse();
  });

  test("rejects duplicate key locations that would otherwise overwrite in issue order", () => {
    const invalid = validSnapshot();
    invalid.keySettings.push({ ...invalid.keySettings[0]! });
    expect(firmwareProfileSnapshotSchema.safeParse(invalid).success).toBeFalse();
  });

  test("rejects firmware-invalid rotary ranges before applying the document", () => {
    const invalid = {
      ...validSnapshot(),
      rotarySettings: {
        rotation_action: 0,
        button_action: 0,
        sensitivity: 0,
        acceleration: 0,
        step_size: 1,
        invert_direction: false,
        rgb_behavior: 0,
        rgb_effect_mode: 1,
        progress_style: 0,
        progress_effect_mode: 1,
        progress_color: [1, 2, 3],
        cw_binding: { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 },
        ccw_binding: { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 },
        click_binding: { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 },
      },
    };
    expect(firmwareProfileSnapshotSchema.safeParse(invalid).success).toBeFalse();
  });

  test("migrates legacy rotary documents and rejects non-boolean filled-only flags", () => {
    const rotarySettings = {
      rotation_action: 0,
      button_action: 0,
      sensitivity: 4,
      acceleration: 0,
      step_size: 1,
      invert_direction: false,
      rgb_behavior: 0,
      rgb_effect_mode: 1,
      progress_style: 0,
      progress_effect_mode: 1,
      progress_color: [1, 2, 3],
      cw_binding: { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 },
      ccw_binding: { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 },
      click_binding: { mode: 0, keycode: 0, modifier_mask_exact: 0, fallback_no_mod_keycode: 0, layer_mode: 0, layer_index: 0 },
    };
    const legacy = firmwareProfileSnapshotSchema.safeParse({ ...validSnapshot(), rotarySettings });
    expect(legacy.success).toBeTrue();
    if (legacy.success) {
      expect(legacy.data.rotarySettings?.progress_filled_only).toBeFalse();
    }
    expect(firmwareProfileSnapshotSchema.safeParse({
      ...validSnapshot(),
      rotarySettings: { ...rotarySettings, progress_filled_only: 1 },
    }).success).toBeFalse();
  });

  test("rejects a snapshot containing mutually recursive on-device macros", () => {
    const actionPrograms = Array.from(
      { length: ACTION_PROGRAM_COUNT },
      () => defaultActionProgram(),
    );
    actionPrograms[0] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 1 }],
    };
    actionPrograms[1] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE }],
    };
    const parsed = firmwareProfileSnapshotSchema.safeParse({
      ...validSnapshot(),
      actionPrograms,
    });
    expect(parsed.success).toBeFalse();
    if (!parsed.success) {
      expect(parsed.error.issues.some((issue) => issue.message.includes("Recursive macro cycle"))).toBeTrue();
    }
  });

  test("defers depth-five import validation to live device capabilities", () => {
    const actionPrograms = Array.from(
      { length: ACTION_PROGRAM_COUNT },
      () => defaultActionProgram(),
    );
    for (let program = 0; program < ACTION_ENGINE_MAX_INSTANCES; program += 1) {
      actionPrograms[program] = {
        version: 1,
        flags: 0,
        steps: [{
          opcode: ActionOpcode.KeyTap,
          arg8: 0,
          arg16: ACTION_MACRO_KEYCODE_BASE + program + 1,
        }],
      };
    }
    const parsed = firmwareProfileSnapshotSchema.safeParse({
      ...validSnapshot(),
      actionPrograms,
    });
    expect(parsed.success).toBeTrue();
  });

  test("validates depth four/five against maxInstances reported by the firmware", async () => {
    const actionPrograms = Array.from(
      { length: ACTION_PROGRAM_COUNT },
      () => defaultActionProgram(),
    );
    actionPrograms[0] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 1 }],
    };
    actionPrograms[1] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 2 }],
    };
    actionPrograms[2] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 3 }],
    };
    actionPrograms[3] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 4 }],
    };

    const originalGetActionCapabilities = kbheDevice.getActionCapabilities;
    const originalGetActiveProfile = kbheDevice.getActiveProfile;
    const originalSetKeySettings = kbheDevice.setKeySettingsExtended;
    let keyWrites = 0;
    let reportedMaxInstances = ACTION_ENGINE_MAX_INSTANCES;
    kbheDevice.getActionCapabilities = async () => actionCapabilities(reportedMaxInstances);
    kbheDevice.getActiveProfile = async () => ({ profile_index: 0, profile_used_mask: 1 });
    kbheDevice.setKeySettingsExtended = async () => {
      keyWrites += 1;
      return false;
    };

    try {
      expect(await applyFirmwareProfileSnapshot({
        ...validSnapshot(),
        actionPrograms,
      }, 0, { persistToFlash: false })).toBeFalse();
      expect(keyWrites).toBe(0);

      /* A future device advertising five instances must pass the same import
       * graph. The forced first key-write failure proves validation advanced
       * past the capability gate without mutating a complete profile. */
      reportedMaxInstances = ACTION_ENGINE_MAX_INSTANCES + 1;
      expect(await applyFirmwareProfileSnapshot({
        ...validSnapshot(),
        actionPrograms,
      }, 0, { persistToFlash: false })).toBeFalse();
      expect(keyWrites).toBe(1);
    } finally {
      kbheDevice.getActionCapabilities = originalGetActionCapabilities;
      kbheDevice.getActiveProfile = originalGetActiveProfile;
      kbheDevice.setKeySettingsExtended = originalSetKeySettings;
    }
  });

  test("clears old macro edges before a crossed acyclic graph migration", async () => {
    const current = Array.from(
      { length: ACTION_PROGRAM_COUNT },
      () => defaultActionProgram(),
    );
    current[1] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE }],
    };
    const target = Array.from(
      { length: ACTION_PROGRAM_COUNT },
      () => defaultActionProgram(),
    );
    target[0] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 1 }],
    };
    expect(findActionProgramCycle(current)).toBeNull();
    expect(findActionProgramCycle(target)).toBeNull();

    const calls: Array<{ index: number; program: ActionProgram }> = [];
    const originalGetActionCapabilities = kbheDevice.getActionCapabilities;
    const originalGetActiveProfile = kbheDevice.getActiveProfile;
    const originalSetKeySettings = kbheDevice.setKeySettingsExtended;
    const originalSetActionProgram = kbheDevice.setActionProgram;
    kbheDevice.getActionCapabilities = async () => actionCapabilities();
    kbheDevice.getActiveProfile = async () => ({ profile_index: 0, profile_used_mask: 1 });
    kbheDevice.setKeySettingsExtended = async () => true;
    kbheDevice.setActionProgram = async (_profile, index, program) => {
      const candidate = current.map((entry) => structuredClone(entry));
      candidate[index] = structuredClone(program);
      if (findActionProgramCycle(candidate)) return false;
      current[index] = structuredClone(program);
      calls.push({ index, program: structuredClone(program) });
      return true;
    };

    try {
      expect(await applyFirmwareProfileSnapshot({
        ...validSnapshot(),
        actionPrograms: target,
      }, 0, { persistToFlash: false })).toBeTrue();
      expect(calls).toHaveLength(ACTION_PROGRAM_COUNT * 2);
      expect(calls.slice(0, ACTION_PROGRAM_COUNT).every(
        ({ program }) => program.steps[0]?.opcode === ActionOpcode.End,
      )).toBeTrue();
      expect(current).toEqual(target);
    } finally {
      kbheDevice.getActionCapabilities = originalGetActionCapabilities;
      kbheDevice.getActiveProfile = originalGetActiveProfile;
      kbheDevice.setKeySettingsExtended = originalSetKeySettings;
      kbheDevice.setActionProgram = originalSetActionProgram;
    }
  });
});
