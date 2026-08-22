import { z } from "zod";

export const ACTION_PROGRAM_VERSION = 1;
export const ACTION_PROFILE_COUNT = 4;
export const ACTION_PROGRAM_COUNT = 16;
export const ACTION_PROGRAM_MAX_STEPS = 32;
export const ACTION_STATE_COUNT = 16;
export const ACTION_ENGINE_MAX_INSTANCES = ACTION_PROGRAM_COUNT;
export const ACTION_OVERLAY_COUNT = 8;
export const ACTION_OVERLAY_MASK_BYTES = 11;
export const ACTION_STEPS_PER_PACKET = 14;
export const ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE = 0x01;
export const ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER = 0x02;
export const ACTION_MACRO_KEYCODE_BASE = 0xf400;
export const ACTION_MACRO_KEYCODE_END = ACTION_MACRO_KEYCODE_BASE + ACTION_PROGRAM_COUNT - 1;

/** Numeric values intentionally mirror action_validation_result_t. */
export enum ActionValidationResult {
  Ok = 0,
  BadVersion = 1,
  TooManySteps = 2,
  BadOpcode = 3,
  BadArgument = 4,
  UnbalancedOutput = 5,
  MacroCycle = 6,
  MacroDepth = 7,
}

export enum ActionOpcode {
  Nop = 0,
  End = 1,
  KeyDown = 2,
  KeyUp = 3,
  KeyTap = 4,
  DelayMs = 5,
  StateSet = 6,
  StateToggle = 7,
  IfStateSkip = 8,
  OverlaySet = 9,
}

export const ACTION_OPCODE_LABELS: Record<ActionOpcode, string> = {
  [ActionOpcode.Nop]: "No operation",
  [ActionOpcode.End]: "End",
  [ActionOpcode.KeyDown]: "Key down",
  [ActionOpcode.KeyUp]: "Key up",
  [ActionOpcode.KeyTap]: "Key tap",
  [ActionOpcode.DelayMs]: "Delay (ms)",
  [ActionOpcode.StateSet]: "Set mode state",
  [ActionOpcode.StateToggle]: "Toggle mode state",
  [ActionOpcode.IfStateSkip]: "If state, else skip",
  [ActionOpcode.OverlaySet]: "Set LED overlay",
};

const byteSchema = z.number().int().min(0).max(0xff);
const u16Schema = z.number().int().min(0).max(0xffff);

export const actionStepSchema = z.object({
  opcode: z.enum(ActionOpcode),
  arg8: byteSchema,
  arg16: u16Schema,
}).strict();

export type ActionStep = z.infer<typeof actionStepSchema>;

export const actionProgramSchema = z.object({
  version: z.literal(ACTION_PROGRAM_VERSION),
  flags: byteSchema.refine(
    (flags) => (flags & ~(ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE | ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER)) === 0,
    "Unknown action-program flags",
  ),
  steps: z.array(actionStepSchema).min(1).max(ACTION_PROGRAM_MAX_STEPS),
}).strict().superRefine((program, ctx) => {
  const outputKeycodes = new Map<number, number>();
  const mustHeldMasks: Array<number | undefined> = Array(program.steps.length).fill(undefined);
  const mayHeldMasks: Array<number | undefined> = Array(program.steps.length).fill(undefined);
  const maxHeldCounts: Array<number | undefined> = Array(program.steps.length).fill(undefined);
  let outputKeycodeCount = 0;

  program.steps.forEach((step, index) => {
    if (
      step.arg16 !== 0
      && (step.opcode === ActionOpcode.KeyDown
        || step.opcode === ActionOpcode.KeyUp
        || step.opcode === ActionOpcode.KeyTap)
      && !outputKeycodes.has(step.arg16)
    ) {
      outputKeycodes.set(step.arg16, outputKeycodeCount++);
    }
    if (
      (step.opcode === ActionOpcode.StateSet || step.opcode === ActionOpcode.StateToggle)
      && step.arg8 >= ACTION_STATE_COUNT
    ) {
      ctx.addIssue({ code: "custom", path: ["steps", index, "arg8"], message: "State index must be 0-15" });
    }
    if (step.opcode === ActionOpcode.OverlaySet && step.arg8 >= ACTION_OVERLAY_COUNT) {
      ctx.addIssue({ code: "custom", path: ["steps", index, "arg8"], message: "Overlay index must be 0-7" });
    }
    if (step.opcode === ActionOpcode.IfStateSkip) {
      const stateIndex = step.arg8 & 0x0f;
      if (
        (step.arg8 & 0x70) !== 0
        || stateIndex >= ACTION_STATE_COUNT
        || index + 1 + step.arg16 > program.steps.length
      ) {
        ctx.addIssue({ code: "custom", path: ["steps", index], message: "Invalid conditional state flags or skip length" });
      }
    }
  });

  if (program.steps.length === 0) return;
  mustHeldMasks[0] = 0;
  mayHeldMasks[0] = 0;
  maxHeldCounts[0] = 0;

  const popcount32 = (value: number): number => {
    let remaining = value >>> 0;
    let count = 0;
    while (remaining !== 0) {
      remaining = (remaining & (remaining - 1)) >>> 0;
      count += 1;
    }
    return count;
  };
  const mergeHeldState = (
    target: number,
    mustHeldMask: number,
    mayHeldMask: number,
    maxHeldCount: number,
  ): void => {
    if (target >= program.steps.length || program.steps[target]?.opcode === ActionOpcode.End) return;
    const normalizedMust = mustHeldMask >>> 0;
    const normalizedMay = mayHeldMask >>> 0;
    if (mustHeldMasks[target] === undefined) {
      mustHeldMasks[target] = normalizedMust;
      mayHeldMasks[target] = normalizedMay;
      maxHeldCounts[target] = maxHeldCount;
    } else {
      mustHeldMasks[target] = ((mustHeldMasks[target] ?? 0) & normalizedMust) >>> 0;
      mayHeldMasks[target] = ((mayHeldMasks[target] ?? 0) | normalizedMay) >>> 0;
      maxHeldCounts[target] = Math.min(
        Math.max(maxHeldCounts[target] ?? 0, maxHeldCount),
        popcount32(mayHeldMasks[target] ?? 0),
      );
    }
  };

  for (let index = 0; index < program.steps.length; index += 1) {
    const step = program.steps[index];
    const incomingMust = mustHeldMasks[index];
    const incomingMay = mayHeldMasks[index];
    const incomingMax = maxHeldCounts[index];
    if (
      step === undefined
      || incomingMust === undefined
      || incomingMay === undefined
      || incomingMax === undefined
      || step.opcode === ActionOpcode.End
    ) continue;

    const keySlot = step.arg16 === 0 ? undefined : outputKeycodes.get(step.arg16);
    const outputBit = keySlot === undefined ? 0 : ((1 << keySlot) >>> 0);
    let outgoingMust = incomingMust >>> 0;
    let outgoingMay = incomingMay >>> 0;
    let outgoingMax = incomingMax;

    if (step.opcode === ActionOpcode.KeyDown && outputBit !== 0 && (outgoingMust & outputBit) === 0) {
      if (outgoingMax >= 8) {
        ctx.addIssue({ code: "custom", path: ["steps", index], message: "At most 8 outputs may be held" });
      }
      outgoingMax += 1;
      outgoingMust = (outgoingMust | outputBit) >>> 0;
      outgoingMay = (outgoingMay | outputBit) >>> 0;
    } else if (step.opcode === ActionOpcode.KeyUp) {
      if ((outgoingMust & outputBit) !== 0 && outgoingMax > 0) outgoingMax -= 1;
      outgoingMust = (outgoingMust & ~outputBit) >>> 0;
      outgoingMay = (outgoingMay & ~outputBit) >>> 0;
    } else if (
      step.opcode === ActionOpcode.KeyTap
      && outputBit !== 0
      && (outgoingMust & outputBit) === 0
      && outgoingMax >= 8
    ) {
      ctx.addIssue({
        code: "custom",
        path: ["steps", index],
        message: "A new tap cannot be allocated while 8 distinct outputs are held",
      });
    }
    outgoingMax = Math.min(outgoingMax, popcount32(outgoingMay));

    if (step.opcode === ActionOpcode.IfStateSkip) {
      mergeHeldState(index + 1, outgoingMust, outgoingMay, outgoingMax);
      mergeHeldState(index + 1 + step.arg16, outgoingMust, outgoingMay, outgoingMax);
    } else {
      mergeHeldState(index + 1, outgoingMust, outgoingMay, outgoingMax);
    }
  }
});

export type ActionProgram = z.infer<typeof actionProgramSchema>;

export interface ActionCapabilities {
  programVersion: number;
  profileCount: number;
  programCount: number;
  maxSteps: number;
  stateCount: number;
  overlayCount: number;
  stepSize: number;
  maxInstances: number;
  profileDocumentSchemaVersion: number;
  atomicProfileDocumentCommit: boolean;
}

export interface ActionProgramMeta {
  profileIndex: number;
  programIndex: number;
  version: number;
  flags: number;
  stepCount: number;
  validationResult: number;
  hash: number;
}

export const actionOverlayBindingSchema = z.object({
  enabled: z.boolean(),
  priority: byteSchema,
  blendMode: z.number().int().min(0).max(2),
  opacity: byteSchema,
  color: z.tuple([byteSchema, byteSchema, byteSchema]),
  allKeys: z.boolean(),
  fadeInMs: u16Schema,
  fadeOutMs: u16Schema,
  keyMask: z.array(byteSchema).length(ACTION_OVERLAY_MASK_BYTES),
  stateIndex: z.number().int().min(0).max(ACTION_STATE_COUNT - 1),
  activeValue: z.number().int().min(0).max(1),
  followsState: z.boolean(),
}).strict();

export type ActionOverlayBinding = z.infer<typeof actionOverlayBindingSchema>;

export function actionProgramMacroDependencies(program: ActionProgram): number {
  const reachable = Array.from({ length: program.steps.length }, () => false);
  reachable[0] = true;
  let dependencies = 0;

  for (let index = 0; index < program.steps.length; index += 1) {
    const step = program.steps[index];
    if (!reachable[index] || !step) continue;
    if (
      (step.opcode === ActionOpcode.KeyDown || step.opcode === ActionOpcode.KeyTap)
      && step.arg16 >= ACTION_MACRO_KEYCODE_BASE
      && step.arg16 <= ACTION_MACRO_KEYCODE_END
    ) {
      dependencies |= 1 << (step.arg16 - ACTION_MACRO_KEYCODE_BASE);
    }
    if (step.opcode === ActionOpcode.End) continue;
    if (index + 1 < program.steps.length) reachable[index + 1] = true;
    if (step.opcode === ActionOpcode.IfStateSkip) {
      const skipped = index + 1 + step.arg16;
      if (skipped < program.steps.length) reachable[skipped] = true;
    }
  }
  return dependencies & 0xffff;
}

/** Return a closed cycle path (slot indexes), or null for an acyclic graph. */
export function findActionProgramCycle(programs: readonly ActionProgram[]): number[] | null {
  if (programs.length !== ACTION_PROGRAM_COUNT) return null;
  const dependencies = programs.map(actionProgramMacroDependencies);
  const colors = Array.from({ length: ACTION_PROGRAM_COUNT }, () => 0);
  const stack: number[] = [];

  const visit = (programIndex: number): number[] | null => {
    colors[programIndex] = 1;
    stack.push(programIndex);
    for (let target = 0; target < ACTION_PROGRAM_COUNT; target += 1) {
      if ((dependencies[programIndex] & (1 << target)) === 0) continue;
      if (colors[target] === 0) {
        const cycle = visit(target);
        if (cycle) return cycle;
      } else if (colors[target] === 1) {
        const start = stack.lastIndexOf(target);
        return [...stack.slice(start), target];
      }
    }
    stack.pop();
    colors[programIndex] = 2;
    return null;
  };

  for (let index = 0; index < ACTION_PROGRAM_COUNT; index += 1) {
    if (colors[index] === 0) {
      const cycle = visit(index);
      if (cycle) return cycle;
    }
  }
  return null;
}

/** Return the first call path that cannot fit in the firmware instance pool. */
export function findActionProgramDepthOverflow(
  programs: readonly ActionProgram[],
  maxDepth = ACTION_ENGINE_MAX_INSTANCES,
): number[] | null {
  if (programs.length !== ACTION_PROGRAM_COUNT || maxDepth < 1) return null;
  const dependencies = programs.map(actionProgramMacroDependencies);

  const visit = (programIndex: number, path: number[], visiting: Set<number>): number[] | null => {
    const nextPath = [...path, programIndex];
    if (nextPath.length > maxDepth) return nextPath;
    visiting.add(programIndex);
    for (let target = 0; target < ACTION_PROGRAM_COUNT; target += 1) {
      if (
        (dependencies[programIndex] & (1 << target)) === 0
        || visiting.has(target)
      ) continue;
      const overflow = visit(target, nextPath, visiting);
      if (overflow) return overflow;
    }
    visiting.delete(programIndex);
    return null;
  };

  for (let program = 0; program < ACTION_PROGRAM_COUNT; program += 1) {
    const overflow = visit(program, [], new Set<number>());
    if (overflow) return overflow;
  }
  return null;
}

export function validateActionProgramGraph(
  programs: readonly ActionProgram[],
): ActionValidationResult {
  if (findActionProgramCycle(programs)) return ActionValidationResult.MacroCycle;
  return findActionProgramDepthOverflow(programs)
    ? ActionValidationResult.MacroDepth
    : ActionValidationResult.Ok;
}

export function defaultActionProgram(): ActionProgram {
  return { version: ACTION_PROGRAM_VERSION, flags: 0, steps: [{ opcode: ActionOpcode.End, arg8: 0, arg16: 0 }] };
}

export function defaultActionOverlayBinding(): ActionOverlayBinding {
  return {
    enabled: false,
    priority: 128,
    blendMode: 0,
    opacity: 255,
    color: [255, 255, 255],
    allKeys: true,
    fadeInMs: 80,
    fadeOutMs: 100,
    keyMask: Array.from({ length: ACTION_OVERLAY_MASK_BYTES }, () => 0),
    stateIndex: 0,
    activeValue: 1,
    followsState: true,
  };
}

export function encodeActionStep(step: ActionStep): number[] {
  return [step.opcode & 0xff, step.arg8 & 0xff, step.arg16 & 0xff, (step.arg16 >>> 8) & 0xff];
}

export function decodeActionStep(bytes: ArrayLike<number>, offset = 0): ActionStep {
  return actionStepSchema.parse({
    opcode: Number(bytes[offset] ?? 0),
    arg8: Number(bytes[offset + 1] ?? 0),
    arg16: Number(bytes[offset + 2] ?? 0) | (Number(bytes[offset + 3] ?? 0) << 8),
  });
}

export function encodeActionProgramWire(programInput: ActionProgram): Uint8Array {
  const program = actionProgramSchema.parse(programInput);
  const wire = new Uint8Array(4 + ACTION_PROGRAM_MAX_STEPS * 4);
  wire[0] = program.version;
  wire[1] = program.flags;
  wire[2] = program.steps.length;
  program.steps.forEach((step, index) => wire.set(encodeActionStep(step), 4 + index * 4));
  return wire;
}

export function actionProgramHash(program: ActionProgram): number {
  let hash = 0x811c9dc5;
  for (const byte of encodeActionProgramWire(program)) {
    hash = Math.imul((hash ^ byte) >>> 0, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}
