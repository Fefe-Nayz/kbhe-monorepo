import { useQuery } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { DistanceSlider } from "@/components/distance-slider";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { CommitSlider } from "@/components/ui/slider";
import { Skeleton } from "@/components/ui/skeleton";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import {
  IconSelectAll,
  IconDeselect,
  IconRestore,
} from "@tabler/icons-react";

export default function Performance() {
  const selectedKeys  = useKeyboardStore((s) => s.selectedKeys);
  const selectAll     = useKeyboardStore((s) => s.selectAll);
  const clearSelection = useKeyboardStore((s) => s.clearSelection);
  const { status }    = useDeviceSession();
  const connected     = status === "connected";
  const { saveState, markSaving, markSaved, markError } = useAutosave();

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

  const filterParamsQ = useQuery({
    queryKey: ["device", "filterParams"],
    queryFn: () => kbheDevice.getFilterParams(),
    enabled: connected,
  });

  const tickRateQ = useQuery({
    queryKey: queryKeys.device.advancedTickRate(),
    queryFn: () => kbheDevice.getAdvancedTickRate(),
    enabled: connected,
  });

  type KeyPatch = Parameters<typeof kbheDevice.setKeySettingsExtended>[1];

  const keyMutation = useOptimisticMutation({
    queryKey: queryKeys.keymap.keySettings(keyIndex ?? -1),
    mutationFn: async (patch: KeyPatch) => {
      if (keyIndex == null) return;
      markSaving();
      await kbheDevice.setKeySettingsExtended(keyIndex, patch);
    },
    optimisticUpdate: (cur, patch) => (cur ? { ...cur, ...patch } : cur!),
    onSuccess: () => markSaved(),
  });
  keyMutation.onError = markError as never;

  const filterMutation = useOptimisticMutation({
    queryKey: queryKeys.device.filterEnabled(),
    mutationFn: (v: boolean) => kbheDevice.setFilterEnabled(v),
    optimisticUpdate: (_cur, v) => v,
  });

  const filterParamsMutation = useOptimisticMutation({
    queryKey: ["device", "filterParams"],
    mutationFn: (params: { noise_band: number; alpha_min_denom: number; alpha_max_denom: number }) =>
      kbheDevice.setFilterParams(params.noise_band, params.alpha_min_denom, params.alpha_max_denom),
    optimisticUpdate: (_cur, params) => params,
  });

  const tickMutation = useOptimisticMutation({
    queryKey: queryKeys.device.advancedTickRate(),
    mutationFn: (v: number) => kbheDevice.setAdvancedTickRate(v),
    optimisticUpdate: (_cur, v) => v,
  });

  const settings = keySettingsQ.data;
  const noSelection = keyIndex == null;

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        <Button variant="outline" size="sm" className="h-8" onClick={selectAll}>
          <IconSelectAll className="size-4" />
          Select All
        </Button>
        <Button variant="outline" size="sm" className="h-8" onClick={clearSelection}>
          <IconDeselect className="size-4" />
          Deselect
        </Button>
      </div>
      <div className="flex items-center gap-2">
        <AutosaveStatus state={saveState} />
        <Button variant="destructive" size="sm" className="h-8 gap-1.5" disabled={noSelection || !connected}>
          <IconRestore className="size-4" />
          Reset Selected
        </Button>
      </div>
    </>
  );

  return (
    <KeyboardEditor
      keyboard={
        <BaseKeyboard
          mode="multi"
          onButtonClick={() => {}}
          showLayerSelector={false}
          showRotary={false}
        />
      }
      menubar={menubar}
    >
      <div className="flex flex-col gap-4">
        <SectionCard
          title={noSelection ? "Per-Key Settings" : `Key ${keyIndex} Performance`}
          description={noSelection ? "Select a key above" : undefined}
        >
          {noSelection ? (
            <p className="text-sm text-muted-foreground py-2">Click any key to configure it.</p>
          ) : keySettingsQ.isLoading ? (
            <div className="space-y-3">{[0,1,2,3].map(i => <Skeleton key={i} className="h-9 w-full" />)}</div>
          ) : !settings ? (
            <p className="text-sm text-muted-foreground">Could not load settings.</p>
          ) : (
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <div className="flex flex-col gap-4">
                <DistanceSlider label="Actuation Point" value={settings.actuation_point_mm}
                  onChange={v => keyMutation.mutate({ actuation_point_mm: v })} disabled={!connected} />
                <DistanceSlider label="Release Point" value={settings.release_point_mm}
                  onChange={v => keyMutation.mutate({ release_point_mm: v })} disabled={!connected} />
                {settings.rapid_trigger_enabled && (
                  <>
                    <DistanceSlider label="RT Press Sensitivity" value={settings.rapid_trigger_press}
                      min={0.01} max={4.0} step={0.01}
                      onChange={v => keyMutation.mutate({ rapid_trigger_press: v })} disabled={!connected} />
                    <DistanceSlider label="RT Release Sensitivity" value={settings.rapid_trigger_release}
                      min={0.01} max={4.0} step={0.01}
                      onChange={v => keyMutation.mutate({ rapid_trigger_release: v })} disabled={!connected} />
                  </>
                )}
              </div>
              <div className="flex flex-col gap-4">
                <FormRow label="Rapid Trigger" description="Dynamic actuation">
                  <Switch checked={settings.rapid_trigger_enabled} disabled={!connected}
                    onCheckedChange={v => keyMutation.mutate({ rapid_trigger_enabled: v })} />
                </FormRow>
                {settings.rapid_trigger_enabled && (
                  <FormRow label="Continuous RT" description="Track past bottom">
                    <Switch checked={settings.continuous_rapid_trigger} disabled={!connected}
                      onCheckedChange={v => keyMutation.mutate({ continuous_rapid_trigger: v })} />
                  </FormRow>
                )}
                <FormRow label="Disable KB on Gamepad">
                  <Switch checked={settings.disable_kb_on_gamepad} disabled={!connected}
                    onCheckedChange={v => keyMutation.mutate({ disable_kb_on_gamepad: v })} />
                </FormRow>
              </div>
            </div>
          )}
        </SectionCard>

        <SectionCard title="Global Performance">
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            <div className="flex flex-col gap-4">
              <FormRow label="Input Filter" description="ADC noise suppression">
                <Switch checked={filterEnabledQ.data ?? false} disabled={!connected}
                  onCheckedChange={v => filterMutation.mutate(v)} />
              </FormRow>
              {filterEnabledQ.data && filterParamsQ.data && (
                <>
                  <div className="grid gap-2">
                    <span className="text-sm font-medium">Noise Band</span>
                    <CommitSlider min={1} max={255} step={1} value={filterParamsQ.data.noise_band}
                      onCommit={v => filterParamsMutation.mutate({ ...filterParamsQ.data!, noise_band: v })}
                      disabled={!connected} />
                  </div>
                  <div className="grid gap-2">
                    <span className="text-sm font-medium">Alpha Min Denom</span>
                    <CommitSlider min={1} max={255} step={1} value={filterParamsQ.data.alpha_min_denom}
                      onCommit={v => filterParamsMutation.mutate({ ...filterParamsQ.data!, alpha_min_denom: v })}
                      disabled={!connected} />
                  </div>
                  <div className="grid gap-2">
                    <span className="text-sm font-medium">Alpha Max Denom</span>
                    <CommitSlider min={1} max={255} step={1} value={filterParamsQ.data.alpha_max_denom}
                      onCommit={v => filterParamsMutation.mutate({ ...filterParamsQ.data!, alpha_max_denom: v })}
                      disabled={!connected} />
                  </div>
                </>
              )}
            </div>
            <div className="flex flex-col gap-4">
              <div className="grid gap-2">
                <span className="text-sm font-medium">Advanced Tick Rate</span>
                <CommitSlider min={1} max={100} step={1} value={tickRateQ.data ?? 1}
                  onCommit={v => tickMutation.mutate(v)}
                  disabled={!connected} />
              </div>
            </div>
          </div>
        </SectionCard>
      </div>
    </KeyboardEditor>
  );
}
