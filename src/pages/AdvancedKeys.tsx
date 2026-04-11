import { useEffect, useRef, useCallback } from "react";
import { sliderVal } from "@/lib/utils";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { KEY_BEHAVIORS, HID_KEYCODES, HID_KEYCODE_NAMES } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { Slider } from "@/components/ui/slider";
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";
import { Skeleton } from "@/components/ui/skeleton";
import { Badge } from "@/components/ui/badge";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";

function useDebounced<T>(fn: (v: T) => void, ms: number) {
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  return useCallback((value: T) => {
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => fn(value), ms);
  }, [fn, ms]);
}

function KeycodeSelect({
  value,
  onChange,
  disabled,
  placeholder,
}: {
  value: number;
  onChange: (v: number) => void;
  disabled?: boolean;
  placeholder?: string;
}) {
  const [search, setSearch] = React.useState("");
  const filteredKeys = Object.entries(HID_KEYCODES).filter(([name]) =>
    name.toLowerCase().includes(search.toLowerCase()),
  );

  return (
    <Select
      value={String(value)}
      onValueChange={(v) => onChange(Number(v))}
      disabled={disabled}
    >
      <SelectTrigger className="w-40">
        <SelectValue placeholder={placeholder}>
          {HID_KEYCODE_NAMES[value] ?? `0x${value.toString(16).toUpperCase()}`}
        </SelectValue>
      </SelectTrigger>
      <SelectContent>
        <div className="p-1">
          <Input
            placeholder="Search…"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="h-7 text-xs mb-1"
          />
        </div>
        {filteredKeys.slice(0, 60).map(([name, code]) => (
          <SelectItem key={code} value={String(code)}>{name}</SelectItem>
        ))}
      </SelectContent>
    </Select>
  );
}

import React from "react";

export default function AdvancedKeys() {
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

  const settings = keySettingsQ.data;
  const noSelection = keyIndex == null;
  const mode = settings?.behavior_mode ?? KEY_BEHAVIORS.Normal;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Advanced Keys" description="Tap-hold, toggle, dynamic mapping · select a key" />
        <AutosaveStatus state={saveState} />
      </div>
      <div className="flex-1 overflow-hidden flex flex-col min-h-0">
        <div className="shrink-0 border-b px-4 py-4 overflow-x-auto bg-muted/20">
          <BaseKeyboard mode="single" onButtonClick={() => {}} />
        </div>
        <div className="flex-1 overflow-y-auto p-4">
          <div className="flex flex-col gap-4 max-w-2xl mx-auto">

            <SectionCard
              title={noSelection ? "Key Behavior Mode" : `Key ${keyIndex} — Advanced Behavior`}
              description={noSelection ? "Select a key above" : undefined}
            >
              {noSelection ? (
                <p className="text-sm text-muted-foreground py-2">Click a key to configure its advanced behavior.</p>
              ) : keySettingsQ.isLoading ? (
                <div className="space-y-3">{[0,1,2].map(i=><Skeleton key={i} className="h-9 w-full"/>)}</div>
              ) : !settings ? (
                <p className="text-sm text-muted-foreground">Could not load settings — check connection.</p>
              ) : (
                <div className="flex flex-col divide-y">
                  <FormRow label="Behavior Mode" description="How this key behaves when pressed">
                    <Select
                      value={String(mode)}
                      disabled={!connected}
                      onValueChange={v => keyMutation.mutate({ behavior_mode: Number(v) })}
                    >
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(KEY_BEHAVIORS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>

                  {/* Tap-Hold options */}
                  {mode === KEY_BEHAVIORS["Tap-Hold"] && (
                    <>
                      <FormRow label="Secondary Action" description="Key sent on hold">
                        <KeycodeSelect
                          value={settings.secondary_hid_keycode}
                          onChange={v => keyMutation.mutate({ secondary_hid_keycode: v })}
                          disabled={!connected}
                          placeholder="Pick key…"
                        />
                      </FormRow>
                      <FormRow label="Hold Threshold" description="Time before hold activates (ms)">
                        <div className="flex items-center gap-2">
                          <Slider
                            min={50} max={1000} step={10}
                            value={[settings.hold_threshold_ms]}
                            onValueChange={(vals) => { const v = sliderVal(vals); if (v !== undefined) writeDebounced({ hold_threshold_ms: v }); }}
                            disabled={!connected}
                            className="w-32"
                          />
                          <span className="text-xs tabular-nums w-14 text-muted-foreground">
                            {settings.hold_threshold_ms} ms
                          </span>
                        </div>
                      </FormRow>
                    </>
                  )}

                  {/* Toggle */}
                  {mode === KEY_BEHAVIORS["Toggle"] && (
                    <FormRow label="Toggle Action" description="Key toggled on each press">
                      <KeycodeSelect
                        value={settings.secondary_hid_keycode}
                        onChange={v => keyMutation.mutate({ secondary_hid_keycode: v })}
                        disabled={!connected}
                        placeholder="Pick key…"
                      />
                    </FormRow>
                  )}

                  {/* Dynamic Mapping */}
                  {mode === KEY_BEHAVIORS["Dynamic Mapping"] && (
                    <>
                      <FormRow label="Zone Count" description="Number of actuation zones (1–4)">
                        <Select
                          value={String(settings.dynamic_zone_count)}
                          disabled={!connected}
                          onValueChange={v => keyMutation.mutate({ dynamic_zone_count: Number(v) })}
                        >
                          <SelectTrigger className="w-24"><SelectValue /></SelectTrigger>
                          <SelectContent>
                            {[1,2,3,4].map(n => <SelectItem key={n} value={String(n)}>{n}</SelectItem>)}
                          </SelectContent>
                        </Select>
                      </FormRow>
                      {Array.from({ length: settings.dynamic_zone_count }).map((_, i) => {
                        const zone = settings.dynamic_zones[i];
                        if (!zone) return null;
                        return (
                          <div key={i} className="py-3">
                            <div className="flex items-center gap-2 mb-2">
                              <Badge variant="outline" className="text-xs">Zone {i + 1}</Badge>
                              <span className="text-xs text-muted-foreground">
                                0 → {zone.end_mm.toFixed(1)} mm
                              </span>
                            </div>
                            <div className="grid grid-cols-2 gap-3 ml-2">
                              <div className="space-y-1">
                                <Label className="text-xs">End depth (mm)</Label>
                                <Slider
                                  min={0.1} max={4.0} step={0.1}
                                  value={[zone.end_mm]}
                                  onValueChange={(vals) => {
                                    const v = sliderVal(vals);
                                    if (v == null) return;
                                    const zones = [...settings.dynamic_zones];
                                    zones[i] = { ...zone, end_mm: v, end_mm_tenths: Math.round(v * 10) };
                                    writeDebounced({ dynamic_zones: zones });
                                  }}
                                  disabled={!connected}
                                />
                              </div>
                              <div className="space-y-1">
                                <Label className="text-xs">Keycode</Label>
                                <KeycodeSelect
                                  value={zone.hid_keycode}
                                  onChange={v => {
                                    const zones = [...settings.dynamic_zones];
                                    zones[i] = { ...zone, hid_keycode: v };
                                    keyMutation.mutate({ dynamic_zones: zones });
                                  }}
                                  disabled={!connected}
                                />
                              </div>
                            </div>
                          </div>
                        );
                      })}
                    </>
                  )}
                </div>
              )}
            </SectionCard>

          </div>
        </div>
      </div>
    </div>
  );
}
