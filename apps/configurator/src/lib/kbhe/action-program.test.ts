import { describe, expect, test } from "bun:test";

import {
  ACTION_MACRO_KEYCODE_BASE,
  ACTION_ENGINE_MAX_INSTANCES,
  ACTION_PROGRAM_COUNT,
  ActionOpcode,
  ActionValidationResult,
  actionProgramMacroDependencies,
  actionProgramHash,
  actionProgramSchema,
  decodeActionStep,
  defaultActionProgram,
  encodeActionStep,
  findActionProgramCycle,
  findActionProgramDepthOverflow,
  validateActionProgramGraph,
} from "./action-program";

describe("action program wire format", () => {
  test("encodes the packed four-byte little-endian step", () => {
    const step = { opcode: ActionOpcode.KeyTap, arg8: 7, arg16: 0x1234 };
    expect(encodeActionStep(step)).toEqual([4, 7, 0x34, 0x12]);
    expect(decodeActionStep(encodeActionStep(step))).toEqual(step);
  });

  test("matches the firmware FNV-1a hash for an empty program", () => {
    expect(actionProgramHash(defaultActionProgram())).toBe(0xeff0c86c);
  });

  test("rejects an out-of-range state and an invalid conditional skip", () => {
    expect(actionProgramSchema.safeParse({
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.StateToggle, arg8: 16, arg16: 0 }],
    }).success).toBeFalse();
    expect(actionProgramSchema.safeParse({
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.IfStateSkip, arg8: 0, arg16: 1 }],
    }).success).toBeFalse();
    expect(actionProgramSchema.safeParse({
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.IfStateSkip, arg8: 0x10, arg16: 0 }],
    }).success).toBeFalse();
  });

  test("tracks distinct held keycodes like the firmware validator", () => {
    const steps = Array.from({ length: 9 }, (_, index) => [
      { opcode: ActionOpcode.KeyDown, arg8: 0, arg16: 0x10 + index },
      { opcode: ActionOpcode.KeyUp, arg8: 0, arg16: 0x80 + index },
    ]).flat();
    expect(actionProgramSchema.safeParse({ version: 1, flags: 0, steps }).success).toBeFalse();
  });

  test("accounts for the temporary binding used by a reachable key tap", () => {
    const held = Array.from({ length: 8 }, (_, index) => ({
      opcode: ActionOpcode.KeyDown,
      arg8: 0,
      arg16: 0x20 + index,
    }));
    expect(actionProgramSchema.safeParse({
      version: 1,
      flags: 0,
      steps: [...held, { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: 0x20 }],
    }).success).toBeTrue();
    expect(actionProgramSchema.safeParse({
      version: 1,
      flags: 0,
      steps: [...held, { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: 0x40 }],
    }).success).toBeFalse();
  });

  test("rejects a conditional path that can exceed held-output capacity", () => {
    const steps = [
      { opcode: ActionOpcode.KeyDown, arg8: 0, arg16: 0x10 },
      { opcode: ActionOpcode.IfStateSkip, arg8: 0, arg16: 1 },
      { opcode: ActionOpcode.KeyUp, arg8: 0, arg16: 0x10 },
      ...Array.from({ length: 8 }, (_, index) => ({
        opcode: ActionOpcode.KeyDown,
        arg8: 0,
        arg16: 0x20 + index,
      })),
      { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
    ];
    expect(actionProgramSchema.safeParse({ version: 1, flags: 0, steps }).success).toBeFalse();
  });

  test("accepts divergent branches that release before reaching capacity", () => {
    const steps = [
      { opcode: ActionOpcode.IfStateSkip, arg8: 0, arg16: 1 },
      { opcode: ActionOpcode.KeyDown, arg8: 0, arg16: 0x10 },
      { opcode: ActionOpcode.KeyUp, arg8: 0, arg16: 0x10 },
      ...Array.from({ length: 8 }, (_, index) => ({
        opcode: ActionOpcode.KeyDown,
        arg8: 0,
        arg16: 0x20 + index,
      })),
      { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
    ];
    expect(actionProgramSchema.safeParse({ version: 1, flags: 0, steps }).success).toBeTrue();
  });

  test("does not count held outputs after a reachable END", () => {
    const steps = [
      { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
      ...Array.from({ length: 9 }, (_, index) => ({
        opcode: ActionOpcode.KeyDown,
        arg8: 0,
        arg16: 0x40 + index,
      })),
    ];
    expect(actionProgramSchema.safeParse({ version: 1, flags: 0, steps }).success).toBeTrue();
  });

  test("rejects direct and mutual reachable macro cycles", () => {
    const programs = Array.from({ length: ACTION_PROGRAM_COUNT }, () => defaultActionProgram());
    programs[0] = {
      version: 1,
      flags: 0,
      steps: [
        { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE },
        { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
      ],
    };
    expect(findActionProgramCycle(programs)).toEqual([0, 0]);
    expect(validateActionProgramGraph(programs)).toBe(ActionValidationResult.MacroCycle);

    programs[0].steps[0]!.arg16 = ACTION_MACRO_KEYCODE_BASE + 1;
    programs[1] = {
      version: 1,
      flags: 0,
      steps: [
        { opcode: ActionOpcode.KeyDown, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE },
        { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
      ],
    };
    expect(findActionProgramCycle(programs)).toEqual([0, 1, 0]);
  });

  test("ignores macro-looking instructions after END", () => {
    const program = {
      version: 1 as const,
      flags: 0,
      steps: [
        { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
        { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE },
      ],
    };
    expect(actionProgramMacroDependencies(program)).toBe(0);
    const programs = Array.from({ length: ACTION_PROGRAM_COUNT }, () => defaultActionProgram());
    programs[0] = program;
    expect(validateActionProgramGraph(programs)).toBe(ActionValidationResult.Ok);
  });

  test("accepts depth four and rejects depth five by default", () => {
    const programs = Array.from({ length: ACTION_PROGRAM_COUNT }, () => defaultActionProgram());
    const call = (target: number) => ({
      version: 1 as const,
      flags: 0,
      steps: [
        { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + target },
        { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
      ],
    });

    for (let index = 0; index < ACTION_ENGINE_MAX_INSTANCES - 1; index += 1) {
      programs[index] = call(index + 1);
    }
    expect(findActionProgramDepthOverflow(programs)).toBeNull();
    expect(validateActionProgramGraph(programs)).toBe(ActionValidationResult.Ok);

    programs[ACTION_ENGINE_MAX_INSTANCES - 1] = call(ACTION_ENGINE_MAX_INSTANCES);
    expect(findActionProgramDepthOverflow(programs)).toEqual([0, 1, 2, 3, 4]);
    expect(validateActionProgramGraph(programs)).toBe(ActionValidationResult.MacroDepth);
  });

  test("accepts fan-out branches within the nesting limit", () => {
    const programs = Array.from({ length: ACTION_PROGRAM_COUNT }, () => defaultActionProgram());
    programs[0] = {
      version: 1,
      flags: 0,
      steps: [
        { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 1 },
        { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 2 },
        { opcode: ActionOpcode.End, arg8: 0, arg16: 0 },
      ],
    };
    programs[1] = {
      version: 1,
      flags: 0,
      steps: [{ opcode: ActionOpcode.KeyTap, arg8: 0, arg16: ACTION_MACRO_KEYCODE_BASE + 3 }],
    };
    programs[2] = programs[1];
    expect(findActionProgramDepthOverflow(programs)).toBeNull();
    expect(validateActionProgramGraph(programs)).toBe(ActionValidationResult.Ok);
  });
});
