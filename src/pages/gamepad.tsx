import { useEffect } from "react";
import { sliderVal } from "@/lib/utils";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import {
  GAMEPAD_AXES, GAMEPAD_BUTTONS, GAMEPAD_DIRECTIONS,
  GAMEPAD_KEYBOARD_ROUTING, GAMEPAD_API_MODES,
} from "@/lib/kbhe/protocol";
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
import { Badge } from "@/components/ui/badge";

export default function Gamepad() {
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

  // Global gamepad settings
  const gamepadQ = useQuery({
    queryKey: queryKeys.gamepad.settings(),
    queryFn: () => kbheDevice.getGamepadSettings(),
    enabled: connected,
  });

  // Per-key gamepad map
  const keyMapQ = useQuery({
    queryKey: queryKeys.gamepad.keyMap(keyIndex ?? -1),
    queryFn: () => keyIndex != null ? kbheDevice.getKeyGamepadMap(keyIndex) : null,
    enabled: connected && keyIndex != null,
  });

  const gamepadMutation = useMutation({
    mutationFn: async (patch: Parameters<typeof kbheDevice.setGamepadSettings>[0]) => {
      markSaving();
      await kbheDevice.setGamepadSettings(patch);
    },
    onSuccess: () => {
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.gamepad.settings() });
    },
    onError: markError,
  });

  const keyMapMutation = useMutation({
    mutationFn: async ({ axis, direction, button }: { axis: number; direction: number; button: number }) => {
      if (keyIndex == null) return;
      markSaving();
      await kbheDevice.setKeyGamepadMap(keyIndex, axis, direction, button);
    },
    onSuccess: () => {
      markSaved();
      if (keyIndex != null) void qc.invalidateQueries({ queryKey: queryKeys.gamepad.keyMap(keyIndex) });
    },
    onError: markError,
  });

  const gs = gamepadQ.data;
  const km = keyMapQ.data;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Gamepad" description="Analog mapping, global settings, per-key axis assignment" />
        <AutosaveStatus state={saveState} />
      </div>

      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs defaultValue="setup" className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4">
            <TabsList className="h-9 mt-1">
              <TabsTrigger value="setup">Setup</TabsTrigger>
              <TabsTrigger value="mapping">Mapping</TabsTrigger>
              <TabsTrigger value="curve">Analog Curve</TabsTrigger>
            </TabsList>
          </div>

          {/* Setup tab */}
          <TabsContent value="setup" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">

              <SectionCard title="API Mode" description="USB gamepad protocol">
                <FormRow label="HID API">
                  {gamepadQ.isLoading ? <Skeleton className="h-8 w-44" /> : (
                    <Select
                      value={String(gs?.api_mode ?? 0)}
                      disabled={!connected}
                      onValueChange={v => gamepadMutation.mutate({ ...gs, api_mode: Number(v) })}
                    >
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(GAMEPAD_API_MODES).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  )}
                </FormRow>
                <Badge variant="outline" className="text-xs mt-1">
                  Changing API mode requires USB re-enumeration
                </Badge>
              </SectionCard>

              <SectionCard title="Keyboard Routing" description="How keyboard output behaves alongside gamepad">
                <FormRow label="Routing Mode">
                  {gamepadQ.isLoading ? <Skeleton className="h-8 w-44" /> : (
                    <Select
                      value={String(gs?.keyboard_routing ?? 1)}
                      disabled={!connected}
                      onValueChange={v => gamepadMutation.mutate({ ...gs, keyboard_routing: Number(v) })}
                    >
                      <SelectTrigger className="w-48"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(GAMEPAD_KEYBOARD_ROUTING).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  )}
                </FormRow>
              </SectionCard>

              <SectionCard title="Stick Options">
                <div className="flex flex-col divide-y">
                  <FormRow label="Square Stick" description="Snap stick output to a square boundary">
                    <Switch
                      checked={gs?.square_mode ?? false}
                      disabled={!connected || gamepadQ.isLoading}
                      onCheckedChange={v => gamepadMutation.mutate({ ...gs, square_mode: v })}
                    />
                  </FormRow>
                  <FormRow label="Reactive Stick / Snappy" description="Enhanced stick response curve">
                    <Switch
                      checked={gs?.reactive_stick ?? false}
                      disabled={!connected || gamepadQ.isLoading}
                      onCheckedChange={v => gamepadMutation.mutate({ ...gs, reactive_stick: v, snappy_mode: v })}
                    />
                  </FormRow>
                </div>
              </SectionCard>

            </div>
          </TabsContent>

          {/* Mapping tab */}
          <TabsContent value="mapping" className="flex-1 overflow-hidden flex flex-col mt-0 min-h-0">
            <div className="shrink-0 border-b px-4 py-4 overflow-x-auto bg-muted/20">
              <BaseKeyboard mode="single" onButtonClick={() => {}} />
            </div>
            <div className="flex-1 overflow-y-auto p-4">
              <div className="flex flex-col gap-4 max-w-2xl mx-auto">
                {keyIndex == null ? (
                  <SectionCard title="Per-Key Gamepad Mapping">
                    <p className="text-sm text-muted-foreground py-2">Select a key to configure its gamepad mapping.</p>
                  </SectionCard>
                ) : (
                  <SectionCard title={`Key ${keyIndex} — Gamepad Map`}>
                    {keyMapQ.isLoading ? (
                      <div className="space-y-3">{[0,1,2].map(i=><Skeleton key={i} className="h-9 w-full"/>)}</div>
                    ) : !km ? (
                      <p className="text-sm text-muted-foreground">Could not load key map.</p>
                    ) : (
                      <div className="flex flex-col divide-y">
                        <FormRow label="Axis">
                          <Select
                            value={String(km.axis)}
                            disabled={!connected}
                            onValueChange={v => keyMapMutation.mutate({ ...km, axis: Number(v) })}
                          >
                            <SelectTrigger className="w-36"><SelectValue /></SelectTrigger>
                            <SelectContent>
                              {Object.entries(GAMEPAD_AXES).map(([name, val]) => (
                                <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                              ))}
                            </SelectContent>
                          </Select>
                        </FormRow>
                        <FormRow label="Direction">
                          <Select
                            value={String(km.direction)}
                            disabled={!connected}
                            onValueChange={v => keyMapMutation.mutate({ ...km, direction: Number(v) })}
                          >
                            <SelectTrigger className="w-24"><SelectValue /></SelectTrigger>
                            <SelectContent>
                              {Object.entries(GAMEPAD_DIRECTIONS).map(([name, val]) => (
                                <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                              ))}
                            </SelectContent>
                          </Select>
                        </FormRow>
                        <FormRow label="Button">
                          <Select
                            value={String(km.button)}
                            disabled={!connected}
                            onValueChange={v => keyMapMutation.mutate({ ...km, button: Number(v) })}
                          >
                            <SelectTrigger className="w-36"><SelectValue /></SelectTrigger>
                            <SelectContent>
                              {Object.entries(GAMEPAD_BUTTONS).map(([name, val]) => (
                                <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                              ))}
                            </SelectContent>
                          </Select>
                        </FormRow>
                      </div>
                    )}
                  </SectionCard>
                )}
              </div>
            </div>
          </TabsContent>

          {/* Analog Curve tab */}
          <TabsContent value="curve" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <SectionCard
                title="Analog Curve"
                description="4-point piecewise linear curve mapping distance to stick output (0–255)"
              >
                {gamepadQ.isLoading ? (
                  <div className="space-y-3">{[0,1,2,3].map(i=><Skeleton key={i} className="h-9 w-full"/>)}</div>
                ) : !gs ? (
                  <p className="text-sm text-muted-foreground">Connect device to edit analog curve.</p>
                ) : (
                  <div className="flex flex-col gap-4">
                    {gs.curve_points.map((pt, i) => (
                      <div key={i} className="grid grid-cols-2 gap-4 p-3 rounded-lg border">
                        <div className="space-y-1">
                          <p className="text-xs font-medium">Point {i + 1} — Distance (mm)</p>
                          <Slider
                            min={0} max={4.0} step={0.01}
                            value={[pt.x_mm]}
                            onValueChange={(vals) => {
                              const v = sliderVal(vals);
                              if (v == null) return;
                              const pts = [...gs.curve_points];
                              pts[i] = { ...pt, x_mm: v, x_01mm: Math.round(v * 100) };
                              gamepadMutation.mutate({ ...gs, curve_points: pts });
                            }}
                            disabled={!connected || (i === 0)}
                          />
                          <p className="text-xs text-muted-foreground tabular-nums">{pt.x_mm.toFixed(2)} mm</p>
                        </div>
                        <div className="space-y-1">
                          <p className="text-xs font-medium">Output (0–255)</p>
                          <Slider
                            min={0} max={255} step={1}
                            value={[pt.y]}
                            onValueChange={(vals) => {
                              const v = sliderVal(vals);
                              if (v == null) return;
                              const pts = [...gs.curve_points];
                              pts[i] = { ...pt, y: v };
                              gamepadMutation.mutate({ ...gs, curve_points: pts });
                            }}
                            disabled={!connected}
                          />
                          <p className="text-xs text-muted-foreground tabular-nums">{pt.y}</p>
                        </div>
                      </div>
                    ))}
                  </div>
                )}
              </SectionCard>
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
