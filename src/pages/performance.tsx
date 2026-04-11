import { useEffect, useRef, useCallback } from "react";
import { sliderVal } from "@/lib/utils";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { SOCD_RESOLUTIONS } from "@/lib/kbhe/protocol";
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

function useDebounced<T>(fn: (v: T) => void, ms: number) {
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  return useCallback((value: T) => {
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => fn(value), ms);
  }, [fn, ms]);
}

function SliderRow({ label, description, value, min, max, step, format, onChange, disabled }: {
  label: string; description?: string; value: number | null;
  min: number; max: number; step: number; format?: (v: number) => string;
  onChange: (v: number) => void; disabled?: boolean;
}) {
  return (
    <FormRow label={label} description={description}>
      <div className="flex items-center gap-3 w-52">
        {value == null ? <Skeleton className="h-5 w-full" /> : (
          <>
            <Slider min={min} max={max} step={step} value={[value]}
              onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) onChange(v); }}
              disabled={disabled} className="flex-1" />
            <span className="text-xs tabular-nums w-12 text-right text-muted-foreground">
              {format ? format(value) : String(value)}
            </span>
          </>
        )}
      </div>
    </FormRow>
  );
}

export default function Performance() {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const setSaveEnabled = useKeyboardStore((s) => s.setSaveEnabled);
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  useEffect(() => { setSaveEnabled(true); return () => setSaveEnabled(false); }, [setSaveEnabled]);

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10) : null;

  const keySettingsQ = useQuery({
    queryKey: queryKeys.keymap.keySettings(keyIndex ?? -1),
    queryFn: () => keyIndex != null ? kbheDevice.getKeySettings(keyIndex) : null,
    enabled: connected && keyIndex != null,
  });

  const filterEnabledQ = useQuery({
    queryKey: queryKeys.device.filterEnabled(),
    queryFn: () => kbheDevice.getFilterEnabled(),
    enabled: connected,
  });

  const tickRateQ = useQuery({
    queryKey: queryKeys.device.advancedTickRate(),
    queryFn: () => kbheDevice.getAdvancedTickRate(),
    enabled: connected,
  });

  const keyMutation = useMutation({
    mutationFn: async (patch: Parameters<typeof kbheDevice.setKeySettingsExtended>[1]) => {
      if (keyIndex == null) return;
      markSaving();
      await kbheDevice.setKeySettingsExtended(keyIndex, patch);
    },
    onSuccess: () => {
      markSaved();
      if (keyIndex != null) void qc.invalidateQueries({ queryKey: queryKeys.keymap.keySettings(keyIndex) });
    },
    onError: markError,
  });

  const writeDebounced = useDebounced(
    (patch: Parameters<typeof kbheDevice.setKeySettingsExtended>[1]) => keyMutation.mutate(patch),
    300,
  );

  const filterMutation = useMutation({
    mutationFn: (v: boolean) => kbheDevice.setFilterEnabled(v),
    onSuccess: () => void qc.invalidateQueries({ queryKey: queryKeys.device.filterEnabled() }),
  });

  const tickMutation = useMutation({
    mutationFn: (v: number) => kbheDevice.setAdvancedTickRate(v),
    onSuccess: () => void qc.invalidateQueries({ queryKey: queryKeys.device.advancedTickRate() }),
  });

  const settings = keySettingsQ.data;
  const noSelection = keyIndex == null;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Performance" description="Actuation, rapid trigger, SOCD · select a key" />
        <AutosaveStatus state={saveState} />
      </div>
      <div className="flex-1 overflow-hidden flex flex-col min-h-0">
        <div className="shrink-0 border-b px-4 py-4 overflow-x-auto bg-muted/20">
          <BaseKeyboard mode="single" onButtonClick={() => {}} />
        </div>
        <div className="flex-1 overflow-y-auto p-4">
          <div className="flex flex-col gap-4 max-w-2xl mx-auto">

            <SectionCard
              title={noSelection ? "Per-Key Settings" : `Key ${keyIndex} — Performance`}
              description={noSelection ? "Select a key above to configure it" : undefined}
            >
              {noSelection ? (
                <p className="text-sm text-muted-foreground py-2">Click any key to configure it.</p>
              ) : keySettingsQ.isLoading ? (
                <div className="space-y-3">{[0,1,2,3].map(i => <Skeleton key={i} className="h-9 w-full" />)}</div>
              ) : !settings ? (
                <p className="text-sm text-muted-foreground">Could not load settings — check connection.</p>
              ) : (
                <div className="flex flex-col divide-y">
                  <SliderRow label="Actuation Point" description="Depth at which keypress registers"
                    value={settings.actuation_point_mm} min={0.1} max={4.0} step={0.1}
                    format={v => `${v.toFixed(1)} mm`} disabled={!connected}
                    onChange={v => writeDebounced({ actuation_point_mm: v })} />
                  <SliderRow label="Release Point" description="Depth at which keypress deregisters"
                    value={settings.release_point_mm} min={0.1} max={4.0} step={0.1}
                    format={v => `${v.toFixed(1)} mm`} disabled={!connected}
                    onChange={v => writeDebounced({ release_point_mm: v })} />
                  <FormRow label="Rapid Trigger" description="Dynamic actuation based on travel direction">
                    <Switch checked={settings.rapid_trigger_enabled} disabled={!connected}
                      onCheckedChange={v => keyMutation.mutate({ rapid_trigger_enabled: v })} />
                  </FormRow>
                  {settings.rapid_trigger_enabled && (<>
                    <SliderRow label="RT Activation Window" value={settings.rapid_trigger_activation}
                      min={0.1} max={4.0} step={0.1} format={v => `${v.toFixed(1)} mm`} disabled={!connected}
                      onChange={v => writeDebounced({ rapid_trigger_activation: v })} />
                    <SliderRow label="RT Press Sensitivity" value={settings.rapid_trigger_press}
                      min={0.01} max={4.0} step={0.01} format={v => `${v.toFixed(2)} mm`} disabled={!connected}
                      onChange={v => writeDebounced({ rapid_trigger_press: v })} />
                    <SliderRow label="RT Release Sensitivity" value={settings.rapid_trigger_release}
                      min={0.01} max={4.0} step={0.01} format={v => `${v.toFixed(2)} mm`} disabled={!connected}
                      onChange={v => writeDebounced({ rapid_trigger_release: v })} />
                    <FormRow label="Continuous Rapid Trigger" description="Track past bottom of travel">
                      <Switch checked={settings.continuous_rapid_trigger} disabled={!connected}
                        onCheckedChange={v => keyMutation.mutate({ continuous_rapid_trigger: v })} />
                    </FormRow>
                  </>)}
                  <FormRow label="Disable KB on Gamepad" description="Mute keyboard when gamepad active">
                    <Switch checked={settings.disable_kb_on_gamepad} disabled={!connected}
                      onCheckedChange={v => keyMutation.mutate({ disable_kb_on_gamepad: v })} />
                  </FormRow>
                  {settings.socd_pair !== null && (
                    <FormRow label="SOCD Resolution" description="Conflict resolution for SOCD pairs">
                      <Select value={String(settings.socd_resolution)} disabled={!connected}
                        onValueChange={v => keyMutation.mutate({ socd_resolution: Number(v) })}>
                        <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                        <SelectContent>
                          {Object.entries(SOCD_RESOLUTIONS).map(([name, val]) => (
                            <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </FormRow>
                  )}
                </div>
              )}
            </SectionCard>

            <SectionCard title="Global Performance" description="Applied to all keys">
              <div className="flex flex-col divide-y">
                <FormRow label="Input Filter" description="ADC noise suppression">
                  <Switch checked={filterEnabledQ.data ?? false} disabled={!connected}
                    onCheckedChange={v => filterMutation.mutate(v)} />
                </FormRow>
                <SliderRow label="Advanced Tick Rate" description="Polling rate multiplier (1 = default)"
                  value={tickRateQ.data ?? null} min={1} max={100} step={1} disabled={!connected}
                  onChange={v => tickMutation.mutate(v)} />
              </div>
            </SectionCard>

          </div>
        </div>
      </div>
    </div>
  );
}
