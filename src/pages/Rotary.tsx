import { useRef, useCallback } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import {
  ROTARY_ACTIONS, ROTARY_BUTTON_ACTIONS, ROTARY_RGB_BEHAVIORS, ROTARY_PROGRESS_STYLES,
  LED_EFFECT_NAMES,
} from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { Slider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";
import { Skeleton } from "@/components/ui/skeleton";
import { sliderVal } from "@/lib/utils";


function useDebounced<T>(fn: (v: T) => void, ms: number) {
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  return useCallback((value: T) => {
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => fn(value), ms);
  }, [fn, ms]);
}

export default function Rotary() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const rotaryQ = useQuery({
    queryKey: queryKeys.rotary.settings(),
    queryFn: () => kbheDevice.getRotaryEncoderSettings(),
    enabled: connected,
  });

  const mutation = useMutation({
    mutationFn: async (patch: Parameters<typeof kbheDevice.setRotaryEncoderSettings>[0]) => {
      markSaving();
      await kbheDevice.setRotaryEncoderSettings(patch);
    },
    onSuccess: () => {
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.rotary.settings() });
    },
    onError: markError,
  });

  const write = (patch: Parameters<typeof kbheDevice.setRotaryEncoderSettings>[0]) => {
    if (!rotaryQ.data) return;
    mutation.mutate({ ...rotaryQ.data, ...patch });
  };

  const writeDebounced = useDebounced(write, 250);

  const s = rotaryQ.data;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Rotary Encoder" description="Rotation, button, RGB, and progress bar settings" />
        <AutosaveStatus state={saveState} />
      </div>

      <div className="flex-1 overflow-y-auto p-4">
        <div className="flex flex-col gap-4 max-w-2xl mx-auto">

          {rotaryQ.isLoading ? (
            <SectionCard>
              <div className="space-y-3">{[0,1,2,3].map(i=><Skeleton key={i} className="h-9 w-full"/>)}</div>
            </SectionCard>
          ) : !s ? (
            <SectionCard>
              <p className="text-sm text-muted-foreground">Connect device to configure rotary encoder.</p>
            </SectionCard>
          ) : (
            <>
              <SectionCard title="Rotation">
                <div className="flex flex-col divide-y">
                  <FormRow label="Rotation Action" description="What rotating the knob controls">
                    <Select value={String(s.rotation_action)} disabled={!connected}
                      onValueChange={v => write({ rotation_action: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(ROTARY_ACTIONS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="Sensitivity">
                    <div className="flex items-center gap-3 w-44">
                      <Slider min={1} max={10} step={1} value={[s.sensitivity]}
                        onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) writeDebounced({ sensitivity: v }); }}
                        disabled={!connected} className="flex-1" />
                      <span className="text-xs tabular-nums w-6 text-muted-foreground">{s.sensitivity}</span>
                    </div>
                  </FormRow>
                  <FormRow label="Step Size">
                    <div className="flex items-center gap-3 w-44">
                      <Slider min={1} max={20} step={1} value={[s.step_size]}
                        onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) writeDebounced({ step_size: v }); }}
                        disabled={!connected} className="flex-1" />
                      <span className="text-xs tabular-nums w-6 text-muted-foreground">{s.step_size}</span>
                    </div>
                  </FormRow>
                  <FormRow label="Invert Direction">
                    <Switch checked={s.invert_direction} disabled={!connected}
                      onCheckedChange={v => write({ invert_direction: v })} />
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Button">
                <FormRow label="Button Action" description="What pressing the knob does">
                  <Select value={String(s.button_action)} disabled={!connected}
                    onValueChange={v => write({ button_action: Number(v) })}>
                    <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                    <SelectContent>
                      {Object.entries(ROTARY_BUTTON_ACTIONS).map(([name, val]) => (
                        <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </FormRow>
              </SectionCard>

              <SectionCard title="RGB Customizer" description="When rotation action is RGB Customizer">
                <div className="flex flex-col divide-y">
                  <FormRow label="RGB Behavior">
                    <Select value={String(s.rgb_behavior)} disabled={!connected}
                      onValueChange={v => write({ rgb_behavior: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(ROTARY_RGB_BEHAVIORS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="RGB Effect Mode">
                    <Select value={String(s.rgb_effect_mode)} disabled={!connected}
                      onValueChange={v => write({ rgb_effect_mode: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                          <SelectItem key={val} value={val}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Progress Bar" description="LED progress bar display">
                <div className="flex flex-col divide-y">
                  <FormRow label="Style">
                    <Select value={String(s.progress_style)} disabled={!connected}
                      onValueChange={v => write({ progress_style: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(ROTARY_PROGRESS_STYLES).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="Effect Mode">
                    <Select value={String(s.progress_effect_mode)} disabled={!connected}
                      onValueChange={v => write({ progress_effect_mode: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                          <SelectItem key={val} value={val}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                </div>
              </SectionCard>
            </>
          )}

        </div>
      </div>
    </div>
  );
}
