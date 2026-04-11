import { useRef, useCallback } from "react";
import { sliderVal } from "@/lib/utils";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { LED_EFFECT_NAMES, LEDEffect } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Switch } from "@/components/ui/switch";
import { Slider } from "@/components/ui/slider";
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";
import { Skeleton } from "@/components/ui/skeleton";
import { Button } from "@/components/ui/button";
import { HexColorPicker } from "react-colorful";

function useDebounced<T>(fn: (v: T) => void, ms: number) {
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  return useCallback((value: T) => {
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => fn(value), ms);
  }, [fn, ms]);
}

function rgbToHex(r: number, g: number, b: number): string {
  return `#${[r, g, b].map(v => v.toString(16).padStart(2, "0")).join("")}`;
}

function hexToRgb(hex: string): [number, number, number] {
  const clean = hex.replace("#", "");
  return [
    parseInt(clean.substring(0, 2), 16) || 0,
    parseInt(clean.substring(2, 4), 16) || 0,
    parseInt(clean.substring(4, 6), 16) || 0,
  ];
}

function ColorPickerField({
  value,
  onChange,
  disabled,
}: {
  value: [number, number, number];
  onChange: (rgb: [number, number, number]) => void;
  disabled?: boolean;
}) {
  const hex = rgbToHex(...value);
  return (
    <details className="relative" open={false}>
      <summary
        className="flex h-8 w-16 cursor-pointer rounded border border-input overflow-hidden list-none"
        style={{ background: hex }}
        aria-disabled={disabled}
      />
      <div className="absolute right-0 z-50 mt-1 rounded border bg-popover p-2 shadow-md">
        <HexColorPicker color={hex} onChange={c => !disabled && onChange(hexToRgb(c))} />
      </div>
    </details>
  );
}

export default function Lighting() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const enabledQ = useQuery({ queryKey: queryKeys.led.enabled(), queryFn: () => kbheDevice.ledGetEnabled(), enabled: connected });
  const brightnessQ = useQuery({ queryKey: queryKeys.led.brightness(), queryFn: () => kbheDevice.ledGetBrightness(), enabled: connected });
  const effectQ = useQuery({ queryKey: queryKeys.led.effect(), queryFn: () => kbheDevice.getLedEffect(), enabled: connected });
  const speedQ = useQuery({ queryKey: queryKeys.led.effectSpeed(), queryFn: () => kbheDevice.getLedEffectSpeed(), enabled: connected });
  const colorQ = useQuery({ queryKey: queryKeys.led.effectColor(), queryFn: () => kbheDevice.getLedEffectColor(), enabled: connected });
  const fpsQ = useQuery({ queryKey: queryKeys.led.fpsLimit(), queryFn: () => kbheDevice.getLedFpsLimit(), enabled: connected });

  const currentEffect = effectQ.data ?? LEDEffect.SOLID;
  const paramsQ = useQuery({
    queryKey: queryKeys.led.effectParams(currentEffect),
    queryFn: () => kbheDevice.getLedEffectParams(currentEffect),
    enabled: connected && effectQ.data != null,
  });

  const enabledMut = useMutation({
    mutationFn: async (v: boolean) => { markSaving(); await kbheDevice.ledSetEnabled(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.enabled() }); },
    onError: markError,
  });

  const brightnessMut = useMutation({
    mutationFn: async (v: number) => { markSaving(); await kbheDevice.ledSetBrightness(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.brightness() }); },
    onError: markError,
  });

  const effectMut = useMutation({
    mutationFn: async (v: number) => { markSaving(); await kbheDevice.setLedEffect(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.effect() }); },
    onError: markError,
  });

  const speedMut = useMutation({
    mutationFn: async (v: number) => { markSaving(); await kbheDevice.setLedEffectSpeed(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.effectSpeed() }); },
    onError: markError,
  });

  const colorMut = useMutation({
    mutationFn: async (rgb: [number, number, number]) => {
      markSaving();
      await kbheDevice.setLedEffectColor(rgb[0], rgb[1], rgb[2]);
    },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.effectColor() }); },
    onError: markError,
  });

  const fpsMut = useMutation({
    mutationFn: async (v: number) => { markSaving(); await kbheDevice.setLedFpsLimit(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.fpsLimit() }); },
    onError: markError,
  });

  const debouncedBrightness = useDebounced((v: number) => brightnessMut.mutate(v), 250);
  const debouncedSpeed = useDebounced((v: number) => speedMut.mutate(v), 250);
  const debouncedFps = useDebounced((v: number) => fpsMut.mutate(v), 250);

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Lighting" description="LED matrix, effects, and parameters" />
        <AutosaveStatus state={saveState} />
      </div>

      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs defaultValue="effects" className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4">
            <TabsList className="h-9 mt-1">
              <TabsTrigger value="effects">Effects</TabsTrigger>
              <TabsTrigger value="matrix">Matrix</TabsTrigger>
            </TabsList>
          </div>

          {/* Effects tab */}
          <TabsContent value="effects" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">

              <SectionCard title="LED Control">
                <div className="flex flex-col divide-y">
                  <FormRow label="LEDs Enabled">
                    <Switch checked={enabledQ.data ?? false} disabled={!connected}
                      onCheckedChange={v => enabledMut.mutate(v)} />
                  </FormRow>
                  <FormRow label="Brightness">
                    <div className="flex items-center gap-3 w-44">
                      {brightnessQ.data == null ? <Skeleton className="h-5 w-full" /> : (
                        <>
                          <Slider min={0} max={255} step={1} value={[brightnessQ.data]}
                            onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) debouncedBrightness(v); }}
                            disabled={!connected} className="flex-1" />
                          <span className="text-xs tabular-nums w-8 text-muted-foreground">
                            {brightnessQ.data}
                          </span>
                        </>
                      )}
                    </div>
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Effect">
                <div className="flex flex-col divide-y">
                  <FormRow label="Mode">
                    {effectQ.isLoading ? <Skeleton className="h-8 w-44" /> : (
                      <Select value={String(effectQ.data ?? 0)} disabled={!connected}
                        onValueChange={v => effectMut.mutate(Number(v))}>
                        <SelectTrigger className="w-48"><SelectValue /></SelectTrigger>
                        <SelectContent>
                          {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                            <SelectItem key={val} value={val}>{name}</SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    )}
                  </FormRow>
                  <FormRow label="Color">
                    {colorQ.data == null ? <Skeleton className="h-8 w-16" /> : (
                      <ColorPickerField
                        value={colorQ.data}
                        onChange={v => colorMut.mutate(v)}
                        disabled={!connected}
                      />
                    )}
                  </FormRow>
                  <FormRow label="Speed (0–255)">
                    <div className="flex items-center gap-3 w-44">
                      {speedQ.data == null ? <Skeleton className="h-5 w-full" /> : (
                        <>
                          <Slider min={0} max={255} step={1} value={[speedQ.data]}
                            onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) debouncedSpeed(v); }}
                            disabled={!connected} className="flex-1" />
                          <span className="text-xs tabular-nums w-8 text-muted-foreground">
                            {speedQ.data}
                          </span>
                        </>
                      )}
                    </div>
                  </FormRow>
                  <FormRow label="FPS Limit">
                    <div className="flex items-center gap-3 w-44">
                      {fpsQ.data == null ? <Skeleton className="h-5 w-full" /> : (
                        <>
                          <Slider min={1} max={60} step={1} value={[fpsQ.data]}
                            onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) debouncedFps(v); }}
                            disabled={!connected} className="flex-1" />
                          <span className="text-xs tabular-nums w-8 text-muted-foreground">
                            {fpsQ.data}
                          </span>
                        </>
                      )}
                    </div>
                  </FormRow>
                </div>
              </SectionCard>

              {paramsQ.data && paramsQ.data.length > 0 && (
                <SectionCard title="Effect Parameters" description={`Parameters for ${LED_EFFECT_NAMES[currentEffect]}`}>
                  <div className="flex flex-col divide-y">
                    {paramsQ.data.map((param, i) => (
                      <FormRow key={i} label={`Parameter ${i + 1}`}>
                        <div className="flex items-center gap-3 w-44">
                          <Slider min={0} max={255} step={1} value={[param]}
                            onValueChange={(vals) => {
                              const v = sliderVal(vals);
                              if (v == null || !paramsQ.data) return;
                              const next = [...paramsQ.data];
                              next[i] = v;
                              void kbheDevice.setLedEffectParams(currentEffect, next);
                            }}
                            disabled={!connected} className="flex-1" />
                          <span className="text-xs tabular-nums w-8 text-muted-foreground">{param}</span>
                        </div>
                      </FormRow>
                    ))}
                  </div>
                </SectionCard>
              )}

            </div>
          </TabsContent>

          {/* Matrix tab */}
          <TabsContent value="matrix" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <SectionCard title="Matrix Editor" description="Per-pixel LED control is available via the device API">
                <div className="flex gap-2 flex-wrap">
                  <Button variant="outline" size="sm" disabled={!connected}
                    onClick={() => void kbheDevice.ledClear()}>
                    Clear All
                  </Button>
                  <Button variant="outline" size="sm" disabled={!connected}
                    onClick={() => void kbheDevice.ledTestRainbow()}>
                    Test Rainbow
                  </Button>
                </div>
                <p className="text-xs text-muted-foreground mt-3">
                  Per-pixel matrix editor coming soon. Use the Effects tab or the API for full control.
                </p>
              </SectionCard>
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
