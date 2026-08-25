import { useEffect, useMemo, useRef, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import {
  IconBraces,
  IconDeviceFloppy,
  IconLayersIntersect,
  IconListNumbers,
  IconPlus,
  IconRefresh,
  IconToggleLeft,
  IconTrash,
} from "@tabler/icons-react";

import { PageContent, PageSection } from "@/components/shared/PageLayout";
import { FormRow, FormRows, SectionCard } from "@/components/shared/SectionCard";
import { cn } from "@/lib/utils";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import {
  patchActiveAppProfileActionOverlay,
  patchActiveAppProfileActionProgram,
  patchActiveAppProfileActionState,
} from "@/lib/kbhe/profile-snapshot-store";
import { runProfileOperation } from "@/lib/kbhe/profile-operation-lock";
import {
  ACTION_OPCODE_LABELS,
  ACTION_ENGINE_MAX_INSTANCES,
  ACTION_OVERLAY_COUNT,
  ACTION_PROGRAM_COUNT,
  ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE,
  ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER,
  ACTION_PROGRAM_MAX_STEPS,
  ACTION_STATE_COUNT,
  ActionOpcode,
  actionProgramSchema,
  findActionProgramCycle,
  findActionProgramDepthOverflow,
  defaultActionOverlayBinding,
  defaultActionProgram,
  type ActionOverlayBinding,
  type ActionProgram,
  type ActionStep,
} from "@/lib/kbhe/action-program";

const opcodeValues = Object.values(ActionOpcode).filter(
  (value): value is ActionOpcode => typeof value === "number",
);

// Base UI shows the raw value in a Select trigger unless Root is handed an
// items map, so every option list here is declared once and shared.
const OPCODE_ITEMS = opcodeValues.map((opcode) => ({
  value: String(opcode),
  label: ACTION_OPCODE_LABELS[opcode],
}));

const PROGRAM_SLOT_ITEMS = Array.from({ length: ACTION_PROGRAM_COUNT }, (_, index) => ({
  value: String(index),
  label: `Macro ${index + 1}`,
}));

const OVERLAY_SLOT_ITEMS = Array.from({ length: ACTION_OVERLAY_COUNT }, (_, index) => ({
  value: String(index),
  label: `Overlay ${index + 1}`,
}));

const OVERLAY_POLARITY_ITEMS = [
  { value: "1", label: "Bit is set" },
  { value: "0", label: "Bit is clear" },
];

const OVERLAY_BLEND_ITEMS = [
  { value: "0", label: "Alpha cross-fade" },
  { value: "1", label: "Additive glow" },
  { value: "2", label: "Replace effect" },
];

function clampInteger(value: number, minimum: number, maximum: number): number {
  if (!Number.isFinite(value)) return minimum;
  return Math.max(minimum, Math.min(maximum, Math.trunc(value)));
}

function parseColor(value: string): [number, number, number] {
  const normalized = /^#[0-9a-f]{6}$/i.test(value) ? value.slice(1) : "ffffff";
  return [
    Number.parseInt(normalized.slice(0, 2), 16),
    Number.parseInt(normalized.slice(2, 4), 16),
    Number.parseInt(normalized.slice(4, 6), 16),
  ];
}

function formatColor(color: readonly number[]): string {
  return `#${color.map((channel) => clampInteger(channel, 0, 255).toString(16).padStart(2, "0")).join("")}`;
}

function maskToText(mask: readonly number[]): string {
  const keys: number[] = [];
  for (let key = 0; key < mask.length * 8; key += 1) {
    if ((mask[Math.floor(key / 8)] & (1 << (key % 8))) !== 0) keys.push(key + 1);
  }
  return keys.join(", ");
}

function textToMask(value: string, byteCount: number): number[] {
  const mask = Array.from({ length: byteCount }, () => 0);
  value.split(/[\s,;]+/).forEach((token) => {
    if (!token) return;
    const oneBased = Number.parseInt(token, 10);
    const key = oneBased - 1;
    if (Number.isInteger(key) && key >= 0 && key < byteCount * 8) {
      mask[Math.floor(key / 8)] |= 1 << (key % 8);
    }
  });
  return mask;
}

function stepArgumentHelp(step: ActionStep): string {
  switch (step.opcode) {
    case ActionOpcode.KeyDown:
    case ActionOpcode.KeyUp:
    case ActionOpcode.KeyTap:
      return "arg16 = HID/custom keycode (decimal or 0x…)";
    case ActionOpcode.DelayMs:
      return "arg16 = delay in milliseconds";
    case ActionOpcode.StateSet:
      return "arg8 = mode 0–15, arg16 = 0/1";
    case ActionOpcode.StateToggle:
      return "arg8 = mode 0–15";
    case ActionOpcode.IfStateSkip:
      return "arg8: mode + expected bit 7; arg16: steps skipped when false";
    case ActionOpcode.OverlaySet:
      return "arg8 = overlay 0–7; arg16: 0 off, 1 on, >1 pulse ms";
    default:
      return "No arguments";
  }
}

function numericInputValue(raw: string): number {
  const value = raw.trim().toLowerCase();
  return Number.parseInt(value, value.startsWith("0x") ? 16 : 10);
}

export default function Macros() {
  const connected = useDeviceSession((state) => state.status === "connected");
  const ramOnlyMode = useDeviceSession((state) => Boolean(state.ramOnlyMode));
  const activeProfileIndex = useDeviceSession((state) => state.activeProfileIndex);
  const queryClient = useQueryClient();
  const [programIndex, setProgramIndex] = useState(0);
  const [overlayIndex, setOverlayIndex] = useState(0);
  const [programDraft, setProgramDraft] = useState<ActionProgram>(defaultActionProgram());
  const [overlayDraft, setOverlayDraft] = useState<ActionOverlayBinding>(defaultActionOverlayBinding());
  const [maskText, setMaskText] = useState("");
  const [savingProgram, setSavingProgram] = useState(false);
  const [savingOverlay, setSavingOverlay] = useState(false);
  const [stateWritePending, setStateWritePending] = useState(false);
  const programWriteRef = useRef(false);
  const overlayWriteRef = useRef(false);
  const stateWriteRef = useRef(false);
  const [message, setMessage] = useState<string | null>(null);

  const profileIndex = activeProfileIndex ?? 0;

  const capabilitiesQ = useQuery({
    queryKey: queryKeys.actions.capabilities(),
    queryFn: () => kbheDevice.getActionCapabilities(),
    enabled: connected,
    staleTime: 30_000,
  });
  const programQ = useQuery({
    queryKey: queryKeys.actions.program(programIndex),
    queryFn: () => kbheDevice.getActionProgram(profileIndex, programIndex),
    enabled: connected && capabilitiesQ.data != null,
  });
  const overlayQ = useQuery({
    queryKey: queryKeys.actions.overlay(overlayIndex),
    queryFn: () => kbheDevice.getActionOverlay(profileIndex, overlayIndex),
    enabled: connected && capabilitiesQ.data != null,
  });
  const statesQ = useQuery({
    queryKey: queryKeys.actions.states(),
    queryFn: () => kbheDevice.getActionStates(),
    enabled: connected && capabilitiesQ.data != null,
    refetchInterval: connected ? 500 : false,
  });

  useEffect(() => {
    setProgramDraft(programQ.data ?? defaultActionProgram());
  }, [programQ.data]);
  useEffect(() => {
    const next = overlayQ.data ?? defaultActionOverlayBinding();
    setOverlayDraft(next);
    setMaskText(maskToText(next.keyMask));
  }, [overlayQ.data]);

  const validation = useMemo(
    () => actionProgramSchema.safeParse(programDraft),
    [programDraft],
  );

  const updateStep = (index: number, patch: Partial<ActionStep>) => {
    setProgramDraft((current) => ({
      ...current,
      steps: current.steps.map((step, stepIndex) =>
        stepIndex === index ? { ...step, ...patch } : step,
      ),
    }));
  };

  const addStep = () => {
    setProgramDraft((current) => {
      if (current.steps.length >= ACTION_PROGRAM_MAX_STEPS) return current;
      const steps = [...current.steps];
      const endIndex = steps.findIndex((step) => step.opcode === ActionOpcode.End);
      const insertAt = endIndex >= 0 ? endIndex : steps.length;
      steps.splice(insertAt, 0, { opcode: ActionOpcode.KeyTap, arg8: 0, arg16: 4 });
      return { ...current, steps };
    });
  };

  const removeStep = (index: number) => {
    setProgramDraft((current) => {
      if (current.steps.length <= 1) return current;
      return { ...current, steps: current.steps.filter((_, stepIndex) => stepIndex !== index) };
    });
  };

  const saveProgram = async () => {
    if (programWriteRef.current) return;
    if (!validation.success) {
      setMessage(validation.error.issues[0]?.message ?? "Invalid program");
      return;
    }
    programWriteRef.current = true;
    setSavingProgram(true);
    setMessage(null);
    try {
      await runProfileOperation(async () => {
        const programs = await Promise.all(
          Array.from({ length: ACTION_PROGRAM_COUNT }, (_, slot) =>
            kbheDevice.getActionProgram(profileIndex, slot),
          ),
        );
        if (programs.some((program) => program == null)) {
          throw new Error("Could not read every macro before validating dependencies");
        }
        const candidatePrograms = programs as ActionProgram[];
        candidatePrograms[programIndex] = validation.data;
        const cycle = findActionProgramCycle(candidatePrograms);
        if (cycle) {
          throw new Error(
            `Recursive macro cycle: ${cycle.map((slot) => `Macro ${slot + 1}`).join(" → ")}`,
          );
        }
        const maxInstances = capabilitiesQ.data?.maxInstances ?? ACTION_ENGINE_MAX_INSTANCES;
        const depthOverflow = findActionProgramDepthOverflow(candidatePrograms, maxInstances);
        if (depthOverflow) {
          throw new Error(
            `Macro nesting exceeds the ${maxInstances}-instance runtime limit: ${depthOverflow.map((slot) => `Macro ${slot + 1}`).join(" → ")}`,
          );
        }
        const ok = await kbheDevice.setActionProgram(
          profileIndex,
          programIndex,
          validation.data,
          !ramOnlyMode,
        );
        if (!ok) throw new Error("Device rejected the program or could not commit it");
        patchActiveAppProfileActionProgram(programIndex, validation.data);
      });
      await queryClient.invalidateQueries({ queryKey: queryKeys.actions.program(programIndex) });
      setMessage(
        ramOnlyMode
          ? `Macro ${programIndex + 1} applied to the temporary app profile.`
          : `Macro ${programIndex + 1} committed on profile ${profileIndex + 1}.`,
      );
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      programWriteRef.current = false;
      setSavingProgram(false);
    }
  };

  const saveOverlay = async () => {
    if (overlayWriteRef.current) return;
    overlayWriteRef.current = true;
    setSavingOverlay(true);
    setMessage(null);
    try {
      const binding = {
        ...overlayDraft,
        keyMask: textToMask(maskText, overlayDraft.keyMask.length),
      };
      await runProfileOperation(async () => {
        const ok = await kbheDevice.setActionOverlay(
          profileIndex,
          overlayIndex,
          binding,
          !ramOnlyMode,
        );
        if (!ok) throw new Error("Device rejected the overlay or could not commit it");
        patchActiveAppProfileActionOverlay(overlayIndex, binding);
      });
      await queryClient.invalidateQueries({ queryKey: queryKeys.actions.overlay(overlayIndex) });
      setMessage(
        ramOnlyMode
          ? `Overlay ${overlayIndex + 1} applied to the temporary app profile.`
          : `Overlay ${overlayIndex + 1} committed on profile ${profileIndex + 1}.`,
      );
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      overlayWriteRef.current = false;
      setSavingOverlay(false);
    }
  };

  const setRuntimeState = async (stateIndex: number, value: boolean) => {
    if (stateWriteRef.current) return;
    stateWriteRef.current = true;
    setStateWritePending(true);
    setMessage(null);
    try {
      await runProfileOperation(async () => {
        if (!(await kbheDevice.setActionState(stateIndex, value))) {
          throw new Error(`Device rejected mode ${stateIndex + 1}`);
        }
        patchActiveAppProfileActionState(stateIndex, value);
      });
      await statesQ.refetch();
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      stateWriteRef.current = false;
      setStateWritePending(false);
    }
  };

  return (
    <PageContent containerClassName="max-w-5xl">
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div className="flex min-w-0 items-start gap-3">
          <div className="flex size-10 shrink-0 items-center justify-center rounded-xl bg-muted text-muted-foreground">
            <IconBraces className="size-5" />
          </div>
          <div className="min-w-0">
            <h2 className="text-lg font-semibold tracking-tight">On-device macros &amp; modes</h2>
            <p className="mt-0.5 text-sm text-muted-foreground">
              Bounded programs that run without the PC. Assign Macro 1–16 from the keymap
              or a rotary binding.
            </p>
          </div>
        </div>
        <div className="flex shrink-0 flex-wrap items-center gap-2">
          <Badge variant="outline">Profile {profileIndex + 1}</Badge>
          {ramOnlyMode && <Badge variant="secondary">Temporary · no flash writes</Badge>}
          <Badge variant={capabilitiesQ.data ? "secondary" : "outline"}>
            {capabilitiesQ.data
              ? `${capabilitiesQ.data.programCount} slots · ${capabilitiesQ.data.maxSteps} steps`
              : "Capabilities unavailable"}
          </Badge>
        </div>
      </div>

      {message && (
        <div className="rounded-lg border bg-muted/40 px-3.5 py-2.5 text-sm">{message}</div>
      )}

      <PageSection title="Macro program">
        <SectionCard
          title="Steps"
          description="The sequence is validated and staged before the firmware commits it. Any held outputs are released if it aborts."
          icon={<IconListNumbers />}
          headerRight={
            <>
              <Select
                value={String(programIndex)}
                items={PROGRAM_SLOT_ITEMS}
                onValueChange={(value) => setProgramIndex(Number(value))}
              >
                <SelectTrigger className="h-8 w-32"><SelectValue /></SelectTrigger>
                <SelectContent>
                  {PROGRAM_SLOT_ITEMS.map((item) => (
                    <SelectItem key={item.value} value={item.value}>{item.label}</SelectItem>
                  ))}
                </SelectContent>
              </Select>
              <Button
                variant="outline"
                size="icon-sm"
                onClick={() => void programQ.refetch()}
                disabled={!connected}
                title="Re-read this macro from the keyboard"
              >
                <IconRefresh />
              </Button>
            </>
          }
          footer={
            <>
              {!validation.success && (
                <p className="mr-auto text-xs text-destructive">
                  {validation.error.issues[0]?.message}
                </p>
              )}
              <Button
                variant="outline"
                size="sm"
                onClick={addStep}
                disabled={programDraft.steps.length >= ACTION_PROGRAM_MAX_STEPS}
              >
                <IconPlus />
                Add step
              </Button>
              <Button
                size="sm"
                onClick={() => void saveProgram()}
                disabled={!connected || savingProgram || !validation.success}
              >
                <IconDeviceFloppy />
                {savingProgram ? "Committing…" : "Commit macro"}
              </Button>
            </>
          }
        >
          <div className="flex flex-col gap-4">
            <FormRows>
              <FormRow
                label="Cancel on key release"
                description="Abort the program and release every owned output when the trigger key comes back up."
              >
                <Switch
                  checked={(programDraft.flags & ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE) !== 0}
                  onCheckedChange={(checked) => setProgramDraft((current) => ({
                    ...current,
                    flags: checked
                      ? current.flags | ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE
                      : current.flags & ~ACTION_PROGRAM_FLAG_CANCEL_ON_RELEASE,
                  }))}
                />
              </FormRow>
              <FormRow
                label="Restart on retrigger"
                description="Pressing the key again cleans up the running instance before starting over."
              >
                <Switch
                  checked={(programDraft.flags & ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER) !== 0}
                  onCheckedChange={(checked) => setProgramDraft((current) => ({
                    ...current,
                    flags: checked
                      ? current.flags | ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER
                      : current.flags & ~ACTION_PROGRAM_FLAG_RESTART_ON_TRIGGER,
                  }))}
                />
              </FormRow>
            </FormRows>

            <div className="flex flex-col gap-2">
              <div className="flex items-center gap-2">
                <h4 className="text-[0.68rem] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
                  Sequence
                </h4>
                <span className="rounded-full bg-muted px-1.5 py-px text-[0.65rem] font-medium tabular-nums text-muted-foreground">
                  {programDraft.steps.length}/{ACTION_PROGRAM_MAX_STEPS}
                </span>
                <div className="h-px flex-1 bg-border" />
              </div>

              {programDraft.steps.map((step, index) => (
                <div
                  key={index}
                  className="rounded-xl border bg-surface-sunken/50 p-3"
                >
                  <div className="flex items-start gap-3">
                    <span className="mt-1.5 flex size-6 shrink-0 items-center justify-center rounded-md bg-muted font-mono text-[0.7rem] font-medium tabular-nums text-muted-foreground">
                      {index + 1}
                    </span>

                    <div className="grid min-w-0 flex-1 gap-2.5 sm:grid-cols-[minmax(0,1.6fr)_minmax(0,1fr)_minmax(0,1fr)]">
                      <label className="grid gap-1">
                        <span className="text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
                          Action
                        </span>
                        <Select
                          value={String(step.opcode)}
                          items={OPCODE_ITEMS}
                          onValueChange={(value) =>
                            updateStep(index, { opcode: Number(value) as ActionOpcode })
                          }
                        >
                          <SelectTrigger className="h-8"><SelectValue /></SelectTrigger>
                          <SelectContent>
                            {OPCODE_ITEMS.map((item) => (
                              <SelectItem key={item.value} value={item.value}>
                                {item.label}
                              </SelectItem>
                            ))}
                          </SelectContent>
                        </Select>
                      </label>

                      <label className="grid gap-1">
                        <span className="text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
                          Arg 8-bit
                        </span>
                        <Input
                          className="h-8 font-mono text-xs"
                          aria-label={`Step ${index + 1} 8-bit argument`}
                          value={step.arg8}
                          onChange={(event) =>
                            updateStep(index, {
                              arg8: clampInteger(numericInputValue(event.target.value), 0, 255),
                            })
                          }
                        />
                      </label>

                      <label className="grid gap-1">
                        <span className="text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
                          Arg 16-bit
                        </span>
                        <Input
                          className="h-8 font-mono text-xs"
                          aria-label={`Step ${index + 1} 16-bit argument`}
                          value={`0x${step.arg16.toString(16).toUpperCase()}`}
                          onChange={(event) =>
                            updateStep(index, {
                              arg16: clampInteger(numericInputValue(event.target.value), 0, 0xffff),
                            })
                          }
                        />
                      </label>
                    </div>

                    <Button
                      variant="ghost"
                      size="icon-sm"
                      className="mt-5 shrink-0"
                      onClick={() => removeStep(index)}
                      disabled={programDraft.steps.length <= 1}
                      title="Remove this step"
                    >
                      <IconTrash />
                    </Button>
                  </div>

                  <p className="mt-2 pl-9 text-xs leading-relaxed text-muted-foreground">
                    {stepArgumentHelp(step)}
                  </p>
                </div>
              ))}
            </div>
          </div>
        </SectionCard>
      </PageSection>

      <PageSection title="LED overlay">
        <SectionCard
          title="State-driven overlay"
          description="Follows a named mode bit and cross-fades over the underlying effect, entirely on the device."
          icon={<IconLayersIntersect />}
          headerRight={
            <Select
              value={String(overlayIndex)}
              items={OVERLAY_SLOT_ITEMS}
              onValueChange={(value) => setOverlayIndex(Number(value))}
            >
              <SelectTrigger className="h-8 w-32"><SelectValue /></SelectTrigger>
              <SelectContent>
                {OVERLAY_SLOT_ITEMS.map((item) => (
                  <SelectItem key={item.value} value={item.value}>{item.label}</SelectItem>
                ))}
              </SelectContent>
            </Select>
          }
          footer={
            <Button
              size="sm"
              onClick={() => void saveOverlay()}
              disabled={!connected || savingOverlay}
            >
              <IconDeviceFloppy />
              {savingOverlay ? "Committing…" : "Commit overlay"}
            </Button>
          }
        >
          <div className="flex flex-col gap-5">
            <div>
              <SubHeading>Trigger</SubHeading>
              <FormRows>
                <FormRow
                  label="Enabled"
                  description="Turn this overlay slot on."
                >
                  <Switch
                    checked={overlayDraft.enabled}
                    onCheckedChange={(enabled) =>
                      setOverlayDraft((current) => ({ ...current, enabled }))
                    }
                  />
                </FormRow>
                <FormRow
                  label="Follow mode state"
                  description="Show the overlay only while the mode bit below matches."
                >
                  <Switch
                    checked={overlayDraft.followsState}
                    onCheckedChange={(followsState) =>
                      setOverlayDraft((current) => ({ ...current, followsState }))
                    }
                  />
                </FormRow>
                <FormRow
                  label="Mode bit"
                  description={`Which of the ${ACTION_STATE_COUNT} mode bits to watch.`}
                >
                  <Input
                    type="number"
                    className="h-8 w-24 font-mono text-xs"
                    min={0}
                    max={ACTION_STATE_COUNT - 1}
                    value={overlayDraft.stateIndex}
                    onChange={(event) =>
                      setOverlayDraft((current) => ({
                        ...current,
                        stateIndex: clampInteger(
                          Number(event.target.value),
                          0,
                          ACTION_STATE_COUNT - 1,
                        ),
                      }))
                    }
                  />
                </FormRow>
                <FormRow
                  label="Shows when"
                  description="Whether the overlay appears on a set or a cleared bit."
                >
                  <Select
                    value={String(overlayDraft.activeValue ? 1 : 0)}
                    items={OVERLAY_POLARITY_ITEMS}
                    onValueChange={(value) =>
                      setOverlayDraft((current) => ({ ...current, activeValue: Number(value) }))
                    }
                  >
                    <SelectTrigger className="h-8 w-40"><SelectValue /></SelectTrigger>
                    <SelectContent>
                      {OVERLAY_POLARITY_ITEMS.map((item) => (
                        <SelectItem key={item.value} value={item.value}>{item.label}</SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </FormRow>
              </FormRows>
            </div>

            <div>
              <SubHeading>Appearance</SubHeading>
              <FormRows>
                <FormRow label="Colour" description="Rendered on top of the running effect.">
                  <Input
                    type="color"
                    className="h-8 w-16 p-1"
                    value={formatColor(overlayDraft.color)}
                    onChange={(event) =>
                      setOverlayDraft((current) => ({
                        ...current,
                        color: parseColor(event.target.value),
                      }))
                    }
                  />
                </FormRow>
                <FormRow
                  label="Blend"
                  description={
                    overlayDraft.blendMode === 1
                      ? "Adds light to the running effect without darkening it."
                      : overlayDraft.blendMode === 2
                        ? "Cross-fades to a replacement colour with no underlying effect left at the end."
                        : "Cross-fades the colour transparently over the running effect."
                  }
                >
                  <Select
                    value={String(overlayDraft.blendMode)}
                    items={OVERLAY_BLEND_ITEMS}
                    onValueChange={(value) =>
                      setOverlayDraft((current) => ({
                        ...current,
                        blendMode: Number(value),
                      }))
                    }
                  >
                    <SelectTrigger className="h-8 w-44"><SelectValue /></SelectTrigger>
                    <SelectContent>
                      {OVERLAY_BLEND_ITEMS.map((item) => (
                        <SelectItem key={item.value} value={item.value}>{item.label}</SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </FormRow>
                <FormRow
                  label="Priority"
                  description="Higher values are composited last when overlays overlap."
                >
                  <Input
                    type="number"
                    className="h-8 w-24 font-mono text-xs"
                    min={0}
                    max={255}
                    value={overlayDraft.priority}
                    onChange={(event) =>
                      setOverlayDraft((current) => ({
                        ...current,
                        priority: clampInteger(Number(event.target.value), 0, 255),
                      }))
                    }
                  />
                </FormRow>
                <FormRow
                  label="Opacity"
                  description={
                    overlayDraft.blendMode === 2
                      ? "Scales the replacement colour from black (0) to full intensity (255)."
                      : "0 is invisible; 255 uses the full overlay colour."
                  }
                >
                  <Input
                    type="number"
                    className="h-8 w-24 font-mono text-xs"
                    min={0}
                    max={255}
                    value={overlayDraft.opacity}
                    onChange={(event) =>
                      setOverlayDraft((current) => ({
                        ...current,
                        opacity: clampInteger(Number(event.target.value), 0, 255),
                      }))
                    }
                  />
                </FormRow>
                <FormRow label="Fade in" description="Cross-fade time when the overlay appears.">
                  <div className="flex items-center gap-1.5">
                    <Input
                      type="number"
                      className="h-8 w-24 font-mono text-xs"
                      min={0}
                      max={2000}
                      value={overlayDraft.fadeInMs}
                      onChange={(event) =>
                        setOverlayDraft((current) => ({
                          ...current,
                          fadeInMs: clampInteger(Number(event.target.value), 0, 2000),
                        }))
                      }
                    />
                    <span className="text-xs text-muted-foreground">ms</span>
                  </div>
                </FormRow>
                <FormRow label="Fade out" description="Cross-fade time when it goes away.">
                  <div className="flex items-center gap-1.5">
                    <Input
                      type="number"
                      className="h-8 w-24 font-mono text-xs"
                      min={0}
                      max={2000}
                      value={overlayDraft.fadeOutMs}
                      onChange={(event) =>
                        setOverlayDraft((current) => ({
                          ...current,
                          fadeOutMs: clampInteger(Number(event.target.value), 0, 2000),
                        }))
                      }
                    />
                    <span className="text-xs text-muted-foreground">ms</span>
                  </div>
                </FormRow>
              </FormRows>
            </div>

            <div>
              <SubHeading>Scope</SubHeading>
              <FormRows>
                <FormRow
                  label="Every key"
                  description="Light the whole board instead of a chosen set."
                >
                  <Switch
                    checked={overlayDraft.allKeys}
                    onCheckedChange={(allKeys) =>
                      setOverlayDraft((current) => ({ ...current, allKeys }))
                    }
                  />
                </FormRow>
                <FormRow
                  stacked
                  label="Key numbers"
                  description="Comma-separated key indices, used when “Every key” is off."
                >
                  <Input
                    className="font-mono text-xs"
                    placeholder="1, 2, 14, 40"
                    value={maskText}
                    disabled={overlayDraft.allKeys}
                    onChange={(event) => setMaskText(event.target.value)}
                  />
                </FormRow>
              </FormRows>
            </div>
          </div>
        </SectionCard>
      </PageSection>

      <PageSection title="Testing">
        <SectionCard
          title="Live mode states"
          description="Flip mode bits by hand to check conditionals and LED indicators without running a macro."
          icon={<IconToggleLeft />}
        >
          <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 lg:grid-cols-8">
            {Array.from({ length: ACTION_STATE_COUNT }, (_, stateIndex) => {
              const active = Boolean((statesQ.data?.bits ?? 0) & (1 << stateIndex));
              return (
                <button
                  key={stateIndex}
                  type="button"
                  aria-pressed={active}
                  onClick={() => void setRuntimeState(stateIndex, !active)}
                  disabled={!connected || stateWritePending}
                  className={cn(
                    "flex items-center justify-center gap-1.5 rounded-lg border px-2 py-1.5 text-xs font-medium transition-colors",
                    "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60",
                    "disabled:pointer-events-none disabled:opacity-45",
                    active
                      ? "border-success/40 bg-success/12 text-success"
                      : "border-border bg-card text-muted-foreground hover:bg-muted hover:text-foreground",
                  )}
                >
                  <span
                    className={cn(
                      "size-1.5 rounded-full",
                      active ? "bg-success" : "bg-muted-foreground/30",
                    )}
                  />
                  Mode {stateIndex + 1}
                </button>
              );
            })}
          </div>
        </SectionCard>
      </PageSection>
    </PageContent>
  );
}

/** Small divider heading used to break long cards into named runs. */
function SubHeading({ children }: { children: React.ReactNode }) {
  return (
    <div className="mb-1 flex items-center gap-2">
      <h4 className="text-[0.68rem] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
        {children}
      </h4>
      <div className="h-px flex-1 bg-border" />
    </div>
  );
}
