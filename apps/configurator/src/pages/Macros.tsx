import { useEffect, useMemo, useRef, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import {
  IconBraces,
  IconDeviceFloppy,
  IconPlus,
  IconRefresh,
  IconTrash,
} from "@tabler/icons-react";

import { PageContent } from "@/components/shared/PageLayout";
import { FormRow, SectionCard } from "@/components/shared/SectionCard";
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
        <div>
          <div className="flex items-center gap-2">
            <IconBraces className="size-5" />
            <h2 className="text-lg font-semibold">On-device macros & modes</h2>
          </div>
          <p className="mt-1 text-sm text-muted-foreground">
            Bounded programs run without the PC. Assign Macro 1–16 from the keymap or a rotary binding.
          </p>
        </div>
        <div className="flex gap-2">
          <Badge variant="outline">Profile {profileIndex + 1}</Badge>
          {ramOnlyMode && <Badge variant="secondary">Temporary · no flash writes</Badge>}
          <Badge variant={capabilitiesQ.data ? "secondary" : "outline"}>
            {capabilitiesQ.data ? `${capabilitiesQ.data.programCount} slots · ${capabilitiesQ.data.maxSteps} steps` : "Unavailable"}
          </Badge>
        </div>
      </div>

      {message && <div className="rounded-lg border bg-muted/40 px-3 py-2 text-sm">{message}</div>}

      <SectionCard
        title="Macro program"
        description="Sequences are validated and staged before the firmware commits them. Any held outputs are released on abort."
        headerRight={
          <div className="flex items-center gap-2">
            <Select value={String(programIndex)} onValueChange={(value) => setProgramIndex(Number(value))}>
              <SelectTrigger className="w-32"><SelectValue /></SelectTrigger>
              <SelectContent>
                {Array.from({ length: ACTION_PROGRAM_COUNT }, (_, index) => (
                  <SelectItem key={index} value={String(index)}>Macro {index + 1}</SelectItem>
                ))}
              </SelectContent>
            </Select>
            <Button variant="outline" size="icon" onClick={() => void programQ.refetch()} disabled={!connected}>
              <IconRefresh />
            </Button>
          </div>
        }
      >
        <div className="grid gap-3">
          <FormRow label="Cancel on key release" description="Abort the program and release every owned output when its trigger is released.">
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
          <FormRow label="Restart on retrigger" description="A second press cleans up the running instance before starting again.">
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

          <div className="grid gap-2">
            {programDraft.steps.map((step, index) => (
              <div key={index} className="grid gap-2 rounded-lg border p-3 md:grid-cols-[3rem_1fr_7rem_8rem_2rem] md:items-center">
                <span className="text-xs font-mono text-muted-foreground">#{index + 1}</span>
                <Select
                  value={String(step.opcode)}
                  onValueChange={(value) => updateStep(index, { opcode: Number(value) as ActionOpcode })}
                >
                  <SelectTrigger><SelectValue /></SelectTrigger>
                  <SelectContent>
                    {opcodeValues.map((opcode) => (
                      <SelectItem key={opcode} value={String(opcode)}>{ACTION_OPCODE_LABELS[opcode]}</SelectItem>
                    ))}
                  </SelectContent>
                </Select>
                <Input
                  aria-label={`Step ${index + 1} arg8`}
                  value={step.arg8}
                  onChange={(event) => updateStep(index, { arg8: clampInteger(numericInputValue(event.target.value), 0, 255) })}
                />
                <Input
                  aria-label={`Step ${index + 1} arg16`}
                  value={`0x${step.arg16.toString(16).toUpperCase()}`}
                  onChange={(event) => updateStep(index, { arg16: clampInteger(numericInputValue(event.target.value), 0, 0xffff) })}
                />
                <Button variant="ghost" size="icon-sm" onClick={() => removeStep(index)} disabled={programDraft.steps.length <= 1}>
                  <IconTrash />
                </Button>
                <p className="text-xs text-muted-foreground md:col-start-2 md:col-span-4">{stepArgumentHelp(step)}</p>
              </div>
            ))}
          </div>

          {!validation.success && (
            <p className="text-sm text-destructive">{validation.error.issues[0]?.message}</p>
          )}
          <div className="flex flex-wrap justify-between gap-2">
            <Button variant="outline" onClick={addStep} disabled={programDraft.steps.length >= ACTION_PROGRAM_MAX_STEPS}>
              <IconPlus /> Add action
            </Button>
            <Button onClick={() => void saveProgram()} disabled={!connected || savingProgram || !validation.success}>
              <IconDeviceFloppy /> {savingProgram ? "Committing…" : "Commit macro"}
            </Button>
          </div>
        </div>
      </SectionCard>

      <SectionCard
        title="State-driven LED overlay"
        description="An overlay follows a named mode bit and cross-fades over the underlying effect entirely on the device."
        headerRight={
          <Select value={String(overlayIndex)} onValueChange={(value) => setOverlayIndex(Number(value))}>
            <SelectTrigger className="w-32"><SelectValue /></SelectTrigger>
            <SelectContent>
              {Array.from({ length: ACTION_OVERLAY_COUNT }, (_, index) => (
                <SelectItem key={index} value={String(index)}>Overlay {index + 1}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        }
      >
        <div className="grid gap-3 md:grid-cols-2">
          <FormRow label="Enabled"><Switch checked={overlayDraft.enabled} onCheckedChange={(enabled) => setOverlayDraft((current) => ({ ...current, enabled }))} /></FormRow>
          <FormRow label="Follow mode state"><Switch checked={overlayDraft.followsState} onCheckedChange={(followsState) => setOverlayDraft((current) => ({ ...current, followsState }))} /></FormRow>
          <label className="grid gap-1 text-sm">Mode index (0–15)<Input type="number" min={0} max={ACTION_STATE_COUNT - 1} value={overlayDraft.stateIndex} onChange={(event) => setOverlayDraft((current) => ({ ...current, stateIndex: clampInteger(Number(event.target.value), 0, ACTION_STATE_COUNT - 1) }))} /></label>
          <label className="grid gap-1 text-sm">Active value<Select value={String(overlayDraft.activeValue ? 1 : 0)} onValueChange={(value) => setOverlayDraft((current) => ({ ...current, activeValue: Number(value) }))}><SelectTrigger><SelectValue /></SelectTrigger><SelectContent><SelectItem value="1">On when set</SelectItem><SelectItem value="0">On when clear</SelectItem></SelectContent></Select></label>
          <label className="grid gap-1 text-sm">Color<Input type="color" value={formatColor(overlayDraft.color)} onChange={(event) => setOverlayDraft((current) => ({ ...current, color: parseColor(event.target.value) }))} /></label>
          <label className="grid gap-1 text-sm">Opacity<Input type="number" min={0} max={255} value={overlayDraft.opacity} onChange={(event) => setOverlayDraft((current) => ({ ...current, opacity: clampInteger(Number(event.target.value), 0, 255) }))} /></label>
          <label className="grid gap-1 text-sm">Fade in (ms)<Input type="number" min={0} max={2000} value={overlayDraft.fadeInMs} onChange={(event) => setOverlayDraft((current) => ({ ...current, fadeInMs: clampInteger(Number(event.target.value), 0, 2000) }))} /></label>
          <label className="grid gap-1 text-sm">Fade out (ms)<Input type="number" min={0} max={2000} value={overlayDraft.fadeOutMs} onChange={(event) => setOverlayDraft((current) => ({ ...current, fadeOutMs: clampInteger(Number(event.target.value), 0, 2000) }))} /></label>
          <FormRow label="All keys"><Switch checked={overlayDraft.allKeys} onCheckedChange={(allKeys) => setOverlayDraft((current) => ({ ...current, allKeys }))} /></FormRow>
          <label className="grid gap-1 text-sm md:col-span-2">Key numbers when “All keys” is off<Input placeholder="1, 2, 14, 40" value={maskText} disabled={overlayDraft.allKeys} onChange={(event) => setMaskText(event.target.value)} /></label>
          <div className="flex justify-end md:col-span-2"><Button onClick={() => void saveOverlay()} disabled={!connected || savingOverlay}><IconDeviceFloppy /> {savingOverlay ? "Committing…" : "Commit overlay"}</Button></div>
        </div>
      </SectionCard>

      <SectionCard title="Live mode states" description="Useful for testing conditionals and LED indicators without running a macro.">
        <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 md:grid-cols-8">
          {Array.from({ length: ACTION_STATE_COUNT }, (_, stateIndex) => {
            const active = Boolean((statesQ.data?.bits ?? 0) & (1 << stateIndex));
            return (
              <Button
                key={stateIndex}
                variant={active ? "default" : "outline"}
                onClick={() => void setRuntimeState(stateIndex, !active)}
                disabled={!connected || stateWritePending}
              >
                Mode {stateIndex + 1}
              </Button>
            );
          })}
        </div>
      </SectionCard>
    </PageContent>
  );
}
