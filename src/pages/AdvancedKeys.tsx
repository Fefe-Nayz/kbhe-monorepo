import { useQuery } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { KeycodeAccordion } from "@/components/keycode-accordion";
import { DistanceSlider } from "@/components/distance-slider";
import { KeyTester } from "@/components/key-tester";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { LayerSelect } from "@/components/layer-select";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Switch } from "@/components/ui/switch";
import { CommitSlider } from "@/components/ui/slider";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";

import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type KeySettings } from "@/lib/kbhe/device";
import { KEY_BEHAVIORS, HID_KEYCODE_NAMES, SOCD_RESOLUTIONS, KEY_COUNT } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { selectItems } from "@/lib/utils";
import {
  IconPlus,
  IconToggleLeft,
  IconHandClick,
  IconArrowsSplit2,
} from "@tabler/icons-react";

const BEHAVIOR_INFO = [
  { id: KEY_BEHAVIORS.Normal, label: "Normal", icon: IconPlus, description: "Standard keypress" },
  { id: KEY_BEHAVIORS["Tap-Hold"], label: "Tap-Hold", icon: IconHandClick, description: "Tap for one key, hold for another" },
  { id: KEY_BEHAVIORS.Toggle, label: "Toggle", icon: IconToggleLeft, description: "Toggle on/off with tap" },
  { id: KEY_BEHAVIORS["Dynamic Mapping"], label: "Dynamic Keystroke", icon: IconArrowsSplit2, description: "Up to 4 actions per zone" },
] as const;


export default function AdvancedKeys() {
  const selectedKeys    = useKeyboardStore((s) => s.selectedKeys);
  const currentLayer    = useKeyboardStore((s) => s.currentLayer);
  const setCurrentLayer = useKeyboardStore((s) => s.setCurrentLayer);
  const { status }      = useDeviceSession();
  const connected       = status === "connected";
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10) : null;

  const keySettingsQ = useQuery({
    queryKey: queryKeys.keymap.keySettings(keyIndex ?? -1),
    queryFn: () => keyIndex != null ? kbheDevice.getKeySettings(keyIndex) : null,
    enabled: connected && keyIndex != null,
  });

  const tickRateQ = useQuery({
    queryKey: queryKeys.device.advancedTickRate(),
    queryFn: () => kbheDevice.getAdvancedTickRate(),
    enabled: connected,
  });

  const keyMutation = useOptimisticMutation<KeySettings | null, Partial<KeySettings>>({
    queryKey: queryKeys.keymap.keySettings(keyIndex ?? -1),
    mutationFn: async (patch) => {
      if (keyIndex == null) return;
      markSaving();
      await kbheDevice.setKeySettingsExtended(keyIndex, patch);
    },
    optimisticUpdate: (cur, patch) => cur ? { ...cur, ...patch } : cur ?? null,
    onSuccess: () => markSaved(),
    onError: markError,
  });

  const tickMutation = useOptimisticMutation<number, number>({
    queryKey: queryKeys.device.advancedTickRate(),
    mutationFn: (v) => kbheDevice.setAdvancedTickRate(v),
    optimisticUpdate: (_cur, v) => v,
  });

  const settings = keySettingsQ.data;

  const liveKey = useThrottledCall(async (patch: Partial<KeySettings>) => {
    if (keyIndex == null || !settings) return;
    await kbheDevice.setKeySettingsExtended(keyIndex, { ...settings, ...patch });
  });

  const liveTick = useThrottledCall(async (v: number) => {
    await kbheDevice.setAdvancedTickRate(v);
  });
  const noSelection = keyIndex == null;

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        <LayerSelect value={currentLayer} onChange={setCurrentLayer} />
      </div>
      <div className="flex items-center gap-2">
        <AutosaveStatus state={saveState} />
      </div>
    </>
  );

  function renderBehaviorPicker() {
    return (
      <div className="grid grid-cols-2 gap-3">
        {BEHAVIOR_INFO.map((b) => (
          <Button
            key={b.id}
                      variant={settings?.behavior_mode === b.id ? "default" : "outline"}
            className="h-auto flex-col items-start gap-1 p-4"
            onClick={() => keyMutation.mutate({ behavior_mode: b.id })}
            disabled={!connected || noSelection}
          >
            <div className="flex items-center gap-2">
              <b.icon className="size-4" />
              <span className="font-medium">{b.label}</span>
            </div>
            <span className="text-xs text-muted-foreground font-normal">{b.description}</span>
          </Button>
        ))}
      </div>
    );
  }

  function renderTapHoldConfig() {
    if (!settings) return null;
    return (
      <Tabs defaultValue="bindings" className="w-full">
        <TabsList className="w-full">
          <TabsTrigger value="bindings">Bindings</TabsTrigger>
          <TabsTrigger value="advanced">Advanced</TabsTrigger>
          <TabsTrigger value="tester">Key Tester</TabsTrigger>
        </TabsList>
        <TabsContent value="bindings" className="mt-4">
          <div className="flex flex-col gap-3">
            <FormRow label="Hold Action">
              <Badge variant="secondary" className="font-mono">
                {HID_KEYCODE_NAMES[settings.secondary_hid_keycode] ?? "None"}
              </Badge>
            </FormRow>
            <KeycodeAccordion
              onSelect={(code) => keyMutation.mutate({ secondary_hid_keycode: code })}
              selectedCode={settings.secondary_hid_keycode}
              className="max-h-64"
            />
          </div>
        </TabsContent>
        <TabsContent value="advanced" className="mt-4">
          <div className="flex flex-col gap-4">
            <div className="grid gap-2">
              <Label className="text-sm font-medium">Tapping Term</Label>
              <CommitSlider
                min={50} max={1000} step={10}
                value={settings.hold_threshold_ms}
                onCommit={(v) => keyMutation.mutate({ hold_threshold_ms: v })}
                disabled={!connected}
              />
            </div>
            <div className="grid gap-2">
              <Label className="text-sm font-medium">Tick Rate</Label>
              <CommitSlider min={1} max={100} step={1}
                value={tickRateQ.data ?? 1}
                onCommit={(v) => tickMutation.mutate(v)}
                disabled={!connected}
              />
            </div>
          </div>
        </TabsContent>
        <TabsContent value="tester" className="mt-4">
          <KeyTester />
        </TabsContent>
      </Tabs>
    );
  }

  function renderToggleConfig() {
    if (!settings) return null;
    return (
      <Tabs defaultValue="bindings" className="w-full">
        <TabsList className="w-full">
          <TabsTrigger value="bindings">Bindings</TabsTrigger>
          <TabsTrigger value="advanced">Advanced</TabsTrigger>
          <TabsTrigger value="tester">Key Tester</TabsTrigger>
        </TabsList>
        <TabsContent value="bindings" className="mt-4">
          <KeycodeAccordion
            onSelect={(code) => keyMutation.mutate({ secondary_hid_keycode: code })}
            selectedCode={settings.secondary_hid_keycode}
            className="max-h-64"
          />
        </TabsContent>
        <TabsContent value="advanced" className="mt-4">
          <div className="grid gap-2">
            <Label className="text-sm font-medium">Tapping Term</Label>
            <CommitSlider min={50} max={1000} step={10}
              value={settings.hold_threshold_ms}
              onCommit={(v) => keyMutation.mutate({ hold_threshold_ms: v })}
              disabled={!connected}
            />
          </div>
        </TabsContent>
        <TabsContent value="tester" className="mt-4">
          <KeyTester />
        </TabsContent>
      </Tabs>
    );
  }

  function renderDynamicConfig() {
    if (!settings) return null;
    const zoneCount = settings.dynamic_zone_count ?? 1;
    return (
      <Tabs defaultValue="bindings" className="w-full">
        <TabsList className="w-full">
          <TabsTrigger value="bindings">Zones</TabsTrigger>
          <TabsTrigger value="performance">Performance</TabsTrigger>
          <TabsTrigger value="advanced">Advanced</TabsTrigger>
          <TabsTrigger value="tester">Key Tester</TabsTrigger>
        </TabsList>
        <TabsContent value="bindings" className="mt-4">
          <div className="flex flex-col gap-4">
            <FormRow label="Zone Count">
              <Select value={String(zoneCount)}
                items={[1, 2, 3, 4].map(n => ({ value: String(n), label: String(n) }))}
                onValueChange={(v) => keyMutation.mutate({ dynamic_zone_count: Number(v) })} disabled={!connected}>
                <SelectTrigger className="w-24 h-8 text-sm"><SelectValue /></SelectTrigger>
                <SelectContent>
                  {[1, 2, 3, 4].map((n) => <SelectItem key={n} value={String(n)}>{n}</SelectItem>)}
                </SelectContent>
              </Select>
            </FormRow>
            {settings.dynamic_zones.slice(0, zoneCount).map((zone, i) => (
              <SectionCard key={i} title={`Zone ${i + 1}`}>
                <div className="flex flex-col gap-3">
                  <DistanceSlider
                    label={`Zone ${i + 1} Travel End`}
                    value={zone.end_mm}
                    onChange={() => {}}
                    disabled={!connected}
                  />
                  <FormRow label={`Zone ${i + 1} Action`}>
                    <Badge variant="secondary" className="font-mono">
                      {HID_KEYCODE_NAMES[zone.hid_keycode] ?? "None"}
                    </Badge>
                  </FormRow>
                </div>
              </SectionCard>
            ))}
          </div>
        </TabsContent>
        <TabsContent value="performance" className="mt-4">
          <div className="flex flex-col gap-4">
            <DistanceSlider label="Actuation Point" value={settings.actuation_point_mm}
              onChange={(v) => keyMutation.mutate({ actuation_point_mm: v })} disabled={!connected} />
          </div>
        </TabsContent>
        <TabsContent value="advanced" className="mt-4">
          <div className="grid gap-2">
            <Label className="text-sm font-medium">Tick Rate</Label>
            <CommitSlider min={1} max={100} step={1}
              value={tickRateQ.data ?? 1}
              onCommit={(v) => tickMutation.mutate(v)}
              disabled={!connected}
            />
          </div>
        </TabsContent>
        <TabsContent value="tester" className="mt-4">
          <KeyTester />
        </TabsContent>
      </Tabs>
    );
  }

  function renderNullBindConfig() {
    if (!settings) return null;
    return (
      <Tabs defaultValue="performance" className="w-full">
        <TabsList className="w-full">
          <TabsTrigger value="performance">Performance</TabsTrigger>
          <TabsTrigger value="tester">Key Tester</TabsTrigger>
        </TabsList>
        <TabsContent value="performance" className="mt-4">
          <div className="flex flex-col gap-4">
            <FormRow label="Rapid Trigger">
              <Switch checked={settings.rapid_trigger_enabled} disabled={!connected}
                onCheckedChange={(v) => keyMutation.mutate({ rapid_trigger_enabled: v })} />
            </FormRow>
            <DistanceSlider label="Actuation Point" value={settings.actuation_point_mm}
              onChange={(v) => keyMutation.mutate({ actuation_point_mm: v })} disabled={!connected} />
          </div>
        </TabsContent>
        <TabsContent value="tester" className="mt-4">
          <KeyTester />
        </TabsContent>
      </Tabs>
    );
  }

  function renderConfigForBehavior() {
    if (!settings) return null;
    switch (settings.behavior_mode) {
      case KEY_BEHAVIORS["Tap-Hold"]: return renderTapHoldConfig();
      case KEY_BEHAVIORS.Toggle: return renderToggleConfig();
      case KEY_BEHAVIORS["Dynamic Mapping"]: return renderDynamicConfig();
      default: return renderNullBindConfig();
    }
  }

  return (
    <KeyboardEditor
      keyboard={
        <BaseKeyboard
          mode="single"
          onButtonClick={() => {}}
          showLayerSelector={false}
          showRotary={false}
        />
      }
      menubar={menubar}
    >
      <div className="flex flex-col gap-4">
        <SectionCard
          title={noSelection ? "Advanced Keys" : `Key ${keyIndex} — Advanced`}
          description={noSelection ? "Select a key to configure advanced behaviors" : undefined}
        >
          {noSelection ? (
            <p className="text-sm text-muted-foreground py-2">Click any key to configure it.</p>
          ) : keySettingsQ.isLoading ? (
            <div className="space-y-3">{[0,1,2].map(i => <Skeleton key={i} className="h-12 w-full" />)}</div>
          ) : !settings ? (
            <p className="text-sm text-muted-foreground">Could not load settings.</p>
          ) : (
            <div className="flex flex-col gap-6">
              <div>
                <Label className="text-sm font-medium mb-3 block">Behavior Mode</Label>
                {renderBehaviorPicker()}
              </div>

              {settings.behavior_mode !== KEY_BEHAVIORS.Normal && (
                <div className="border-t pt-4">
                  {renderConfigForBehavior()}
                </div>
              )}
            </div>
          )}
        </SectionCard>

        {!noSelection && settings && (
          <SectionCard title="SOCD" description="Simultaneous Opposing Cardinal Directions">
            <div className="flex flex-col gap-4">
              <FormRow label="Paired Key" description="The partner key for SOCD resolution">
                <Select
                  value={settings.socd_pair !== null ? String(settings.socd_pair) : "none"}
                  items={[{ value: "none", label: "None" }, ...Array.from({ length: KEY_COUNT }, (_, i) => ({
                    value: String(i),
                    label: `Key ${i}`,
                  })).filter((_, i) => i !== keyIndex)]}
                  disabled={!connected}
                  onValueChange={v => keyMutation.mutate({
                    socd_pair: v === "none" ? 255 : Number(v),
                  })}
                >
                  <SelectTrigger className="w-44 h-8 text-sm"><SelectValue /></SelectTrigger>
                  <SelectContent className="max-h-60">
                    <SelectItem value="none">None (disabled)</SelectItem>
                    {Array.from({ length: KEY_COUNT }, (_, i) => i)
                      .filter(i => i !== keyIndex)
                      .map(i => (
                        <SelectItem key={i} value={String(i)}>Key {i}</SelectItem>
                      ))}
                  </SelectContent>
                </Select>
              </FormRow>
              {settings.socd_pair !== null && (
                <>
                  <FormRow label="Resolution Mode" description="How conflicting inputs are resolved">
                    <Select value={String(settings.socd_resolution)} disabled={!connected}
                      items={selectItems(SOCD_RESOLUTIONS)}
                      onValueChange={v => keyMutation.mutate({ socd_resolution: Number(v) })}>
                      <SelectTrigger className="w-44 h-8 text-sm"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(SOCD_RESOLUTIONS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <p className="text-xs text-muted-foreground">
                    {settings.socd_resolution === 1
                      ? "When both paired keys are held, the deeper press wins."
                      : "When both paired keys are held, the last pressed key wins."}
                  </p>
                </>
              )}
              {settings.socd_pair === null && (
                <p className="text-xs text-muted-foreground">
                  SOCD is disabled. Select a paired key to enable simultaneous opposing input resolution.
                </p>
              )}
            </div>
          </SectionCard>
        )}
      </div>
    </KeyboardEditor>
  );
}
