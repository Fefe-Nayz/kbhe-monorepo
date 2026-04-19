import { useCallback, useEffect, useMemo, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { useKeyboardPreviewLegends } from "@/hooks/use-keyboard-preview-legends";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { DistanceSlider } from "@/components/distance-slider";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Checkbox } from "@/components/ui/checkbox";
import { Switch } from "@/components/ui/switch";
import { CommitSlider } from "@/components/ui/commit-slider";
import { Skeleton } from "@/components/ui/skeleton";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useProfileStore } from "@/stores/profileStore";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type KeySettings } from "@/lib/kbhe/device";
import { KEY_COUNT, TRIGGER_CHATTER_GUARD_MAX_MS } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import {
  IconAlertTriangle,
  IconSelectAll,
  IconDeselect,
  IconRestore,
  IconPointer,
} from "@tabler/icons-react";

type FilterParams = { noise_band: number; alpha_min_denom: number; alpha_max_denom: number };
type KeyUpdateVars = {
  patch: Partial<KeySettings>;
  keyIndexes: number[];
  enforceLinkedRapidSensitivity?: boolean;
};

function toHundredths(mm: number): number {
  return Math.round(mm * 100);
}

function hasSeparateReleaseSensitivity(settings: Pick<KeySettings, "rapid_trigger_press" | "rapid_trigger_release">): boolean {
  return toHundredths(settings.rapid_trigger_press) !== toHundredths(settings.rapid_trigger_release);
}

function isRapidTriggerPatch(patch: Partial<KeySettings>): boolean {
  return (
    patch.rapid_trigger_enabled !== undefined
    || patch.rapid_trigger_press !== undefined
    || patch.rapid_trigger_release !== undefined
    || patch.continuous_rapid_trigger !== undefined
  );
}

async function fetchAllDetailedKeySettings(profileIndex: number, layerIndex: number): Promise<KeySettings[]> {
  const settings: KeySettings[] = [];
  const batchSize = 8;

  for (let start = 0; start < KEY_COUNT; start += batchSize) {
    const end = Math.min(start + batchSize, KEY_COUNT);
    const requests = Array.from({ length: end - start }, (_, i) =>
      kbheDevice.getKeySettings(start + i, profileIndex, layerIndex));
    const results = await Promise.all(requests);
    for (const item of results) {
      if (item) settings.push(item);
    }
  }

  return settings;
}

export default function Performance() {
  const queryClient = useQueryClient();
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const currentLayer = useKeyboardStore((s) => s.currentLayer);
  const selectAll = useKeyboardStore((s) => s.selectAll);
  const clearSelection = useKeyboardStore((s) => s.clearSelection);
  const runtimeSource = useProfileStore((s) => s.runtimeSource);
  const status = useDeviceSession((s) => s.status);
  const activeProfileIndex = useDeviceSession((s) => s.activeProfileIndex);
  const profileContext = activeProfileIndex ?? 0;
  const connected = status === "connected";
  const { keyLegendSlotsMap, isLoading: keyboardPreviewLoading } = useKeyboardPreviewLegends();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const selectedKeyIndexes = useMemo(() => {
    const next: number[] = [];

    for (const keyId of selectedKeys) {
      if (!keyId.startsWith("key-")) continue;
      const parsed = Number.parseInt(keyId.replace("key-", ""), 10);
      if (!Number.isFinite(parsed)) continue;
      next.push(parsed);
    }

    return next;
  }, [selectedKeys]);

  const keyIndex = selectedKeyIndexes[0] ?? null;

  // ── Queries ──

  const keySettingsQ = useQuery({
    queryKey: queryKeys.keymap.keySettings(
      keyIndex ?? -1,
      currentLayer,
      profileContext,
      runtimeSource,
    ),
    queryFn: () => keyIndex != null
      ? kbheDevice.getKeySettings(keyIndex, profileContext, currentLayer)
      : null,
    enabled: connected && keyIndex != null,
  });

  const allSettingsQ = useQuery({
    queryKey: queryKeys.keymap.allSettings(currentLayer, profileContext, runtimeSource),
    queryFn: () => fetchAllDetailedKeySettings(profileContext, currentLayer),
    enabled: connected && selectedKeyIndexes.length > 1,
    staleTime: 15_000,
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

  const triggerChatterGuardQ = useQuery({
    queryKey: queryKeys.device.triggerChatterGuard(),
    queryFn: () => kbheDevice.getTriggerChatterGuard(),
    enabled: connected,
  });

  const settings = keySettingsQ.data;
  const noSelection = selectedKeyIndexes.length === 0;
  const isMultiSelection = selectedKeyIndexes.length > 1;

  const settingsByIndex = useMemo(() => {
    const next = new Map<number, KeySettings>();
    for (const entry of allSettingsQ.data ?? []) {
      next.set(entry.key_index, entry);
    }
    return next;
  }, [allSettingsQ.data]);

  const selectedSettings = useMemo(
    () => selectedKeyIndexes
      .map((index) => {
        if (index === keyIndex && settings) return settings;
        return settingsByIndex.get(index) ?? null;
      })
      .filter((item): item is KeySettings => item !== null),
    [keyIndex, selectedKeyIndexes, settings, settingsByIndex],
  );

  const hasCompleteSelectedSettings = !isMultiSelection || selectedSettings.length === selectedKeyIndexes.length;
  const [useSeparateReleaseSensitivity, setUseSeparateReleaseSensitivity] = useState(false);

  const inferredSeparateReleaseSensitivity = useMemo(() => {
    if (selectedSettings.length > 0) {
      return selectedSettings.some((item) => hasSeparateReleaseSensitivity(item));
    }

    return settings ? hasSeparateReleaseSensitivity(settings) : false;
  }, [selectedSettings, settings]);

  const selectedKeySignature = selectedKeyIndexes.join(",");

  useEffect(() => {
    setUseSeparateReleaseSensitivity(inferredSeparateReleaseSensitivity);
  }, [selectedKeySignature, inferredSeparateReleaseSensitivity]);

  const mixedValues = useMemo(() => {
    const hasMixed = <T,>(selector: (item: KeySettings) => T) => {
      if (selectedSettings.length < 2) return false;
      const first = selector(selectedSettings[0]);
      return selectedSettings.some((item) => selector(item) !== first);
    };

    return {
      actuation_point_mm: hasMixed((item) => item.actuation_point_mm),
      release_point_mm: hasMixed((item) => item.release_point_mm),
      rapid_trigger_enabled: hasMixed((item) => item.rapid_trigger_enabled),
      rapid_trigger_press: hasMixed((item) => item.rapid_trigger_press),
      rapid_trigger_release: hasMixed((item) => item.rapid_trigger_release),
      separate_release_sensitivity: hasMixed((item) => hasSeparateReleaseSensitivity(item)),
      continuous_rapid_trigger: hasMixed((item) => item.continuous_rapid_trigger),
    };
  }, [selectedSettings]);

  const mixedLabels = useMemo(() => {
    const labels: string[] = [];
    if (mixedValues.actuation_point_mm) labels.push("Actuation Point");
    if (mixedValues.release_point_mm) labels.push("Release Point");
    if (mixedValues.rapid_trigger_enabled) labels.push("Rapid Trigger");
    if (mixedValues.rapid_trigger_press) labels.push("RT Press Sensitivity");
    if (mixedValues.rapid_trigger_release) labels.push("RT Release Sensitivity");
    if (mixedValues.separate_release_sensitivity) labels.push("Separate Release Sensitivity");
    if (mixedValues.continuous_rapid_trigger) labels.push("Continuous RT");
    return labels;
  }, [mixedValues]);

  const hasMixedValues = mixedLabels.length > 0;
  const rapidTriggerSectionEnabled = !!settings?.rapid_trigger_enabled || mixedValues.rapid_trigger_enabled;

  // ── Live preview (throttled, runtime-only SET) ──
  // The firmware auto-saves to flash 750 ms after the last change via settings_task().
  // No need to call saveSettings() — just flood SET commands freely.

  const liveKeyUpdate = useThrottledCall(async (patch: Partial<KeySettings>) => {
    if (keyIndex == null || !settings) return;

    const effectivePatch = !useSeparateReleaseSensitivity && isRapidTriggerPatch(patch)
      ? {
          ...patch,
          rapid_trigger_release: patch.rapid_trigger_press ?? settings.rapid_trigger_press,
        }
      : patch;

    await kbheDevice.setKeySettingsExtended(keyIndex, {
      ...settings,
      ...effectivePatch,
      profile_index: profileContext,
      layer_index: currentLayer,
    });
  });

  const liveFilterParams = useThrottledCall(async (params: FilterParams) => {
    await kbheDevice.setFilterParams(params.noise_band, params.alpha_min_denom, params.alpha_max_denom);
  });

  const liveTickRate = useThrottledCall(async (v: number) => {
    await kbheDevice.setAdvancedTickRate(v);
  });

  // ── Commit mutations (fire on pointer-up, update query cache) ──
  // TData = query cache type, TVars = call args, TResult = device return type.
  // Always send FULL settings to avoid firmware defaults overwriting unchanged fields.

  const keyMutation = useOptimisticMutation<KeySettings | null, KeyUpdateVars, void>({
    queryKey: queryKeys.keymap.keySettings(
      keyIndex ?? -1,
      currentLayer,
      profileContext,
      runtimeSource,
    ),
    mutationFn: async ({ patch, keyIndexes, enforceLinkedRapidSensitivity = false }) => {
      if (keyIndexes.length === 0) return;
      markSaving();
      await Promise.all(
        keyIndexes.map(async (targetKeyIndex) => {
          const base =
            targetKeyIndex === keyIndex && settings
              ? settings
              : await kbheDevice.getKeySettings(targetKeyIndex, profileContext, currentLayer);

          if (!base) {
            throw new Error(`Unable to load key settings for key ${targetKeyIndex}`);
          }

          const effectivePatch = enforceLinkedRapidSensitivity
            ? {
                ...patch,
                rapid_trigger_release: patch.rapid_trigger_press ?? base.rapid_trigger_press,
              }
            : patch;

          await kbheDevice.setKeySettingsExtended(targetKeyIndex, {
            ...base,
            ...effectivePatch,
            profile_index: profileContext,
            layer_index: currentLayer,
          });
        }),
      );
    },
    optimisticUpdate: (cur, vars) => {
      if (!cur) return cur ?? null;

      const effectivePatch = vars.enforceLinkedRapidSensitivity
        ? {
            ...vars.patch,
            rapid_trigger_release: vars.patch.rapid_trigger_press ?? cur.rapid_trigger_press,
          }
        : vars.patch;

      return { ...cur, ...effectivePatch };
    },
    onSuccess: async (_result, vars) => {
      await Promise.all(
        [
          ...vars.keyIndexes.map((targetKeyIndex) =>
            queryClient.invalidateQueries({
              queryKey: queryKeys.keymap.keySettings(
                targetKeyIndex,
                currentLayer,
                profileContext,
                runtimeSource,
              ),
            })),
          queryClient.invalidateQueries({
            queryKey: queryKeys.keymap.allSettings(currentLayer, profileContext, runtimeSource),
          }),
        ],
      );
      markSaved();
    },
    onError: () => markError(),
  });

  const filterMutation = useOptimisticMutation<boolean, boolean, boolean>({
    queryKey: queryKeys.device.filterEnabled(),
    mutationFn: (v) => kbheDevice.setFilterEnabled(v),
    optimisticUpdate: (_cur, v) => v,
  });

  const filterParamsMutation = useOptimisticMutation<FilterParams, FilterParams, boolean>({
    queryKey: ["device", "filterParams"],
    mutationFn: (p) => kbheDevice.setFilterParams(p.noise_band, p.alpha_min_denom, p.alpha_max_denom),
    optimisticUpdate: (_cur, p) => p,
  });

  const tickMutation = useOptimisticMutation<number, number, boolean>({
    queryKey: queryKeys.device.advancedTickRate(),
    mutationFn: (v) => kbheDevice.setAdvancedTickRate(v),
    optimisticUpdate: (_cur, v) => v,
  });

  const triggerChatterGuardMutation = useOptimisticMutation<
    { enabled: boolean; duration_ms: number } | null,
    { enabled: boolean; duration_ms: number },
    boolean
  >({
    queryKey: queryKeys.device.triggerChatterGuard(),
    mutationFn: async (next) => {
      markSaving();
      return kbheDevice.setTriggerChatterGuard(next.enabled, next.duration_ms);
    },
    optimisticUpdate: (_cur, next) => next,
    onSuccess: () => markSaved(),
    onError: () => markError(),
  });

  const chatterGuardValue = triggerChatterGuardQ.data ?? { enabled: false, duration_ms: 0 };

  const commitChatterGuard = useCallback(
    (patch: Partial<{ enabled: boolean; duration_ms: number }>) => {
      triggerChatterGuardMutation.mutate({
        enabled: patch.enabled ?? chatterGuardValue.enabled,
        duration_ms: patch.duration_ms ?? chatterGuardValue.duration_ms,
      });
    },
    [chatterGuardValue.duration_ms, chatterGuardValue.enabled, triggerChatterGuardMutation],
  );

  // Merge a partial patch with the current full settings and commit.
  function commitKey(
    patch: Partial<KeySettings>,
    options?: { enforceLinkedRapidSensitivity?: boolean },
  ) {
    if (selectedKeyIndexes.length === 0) return;

    const enforceLinkedRapidSensitivity =
      options?.enforceLinkedRapidSensitivity
      ?? (!useSeparateReleaseSensitivity && isRapidTriggerPatch(patch));

    keyMutation.mutate({
      patch,
      keyIndexes: selectedKeyIndexes,
      enforceLinkedRapidSensitivity,
    });
  }

  const resetSelectedPerformance = useCallback(async () => {
    if (!connected || selectedKeyIndexes.length === 0) return;

    try {
      markSaving();

      await Promise.all(
        selectedKeyIndexes.map(async (targetKeyIndex) => {
          const ok = await kbheDevice.resetKeyTriggerSettings(targetKeyIndex);
          if (!ok) {
            throw new Error(`Unable to reset trigger settings for key ${targetKeyIndex}`);
          }
        }),
      );

      await Promise.all([
        ...selectedKeyIndexes.map((targetKeyIndex) =>
          queryClient.invalidateQueries({
            queryKey: queryKeys.keymap.keySettings(
              targetKeyIndex,
              currentLayer,
              profileContext,
              runtimeSource,
            ),
          })),
        queryClient.invalidateQueries({
          queryKey: queryKeys.keymap.allSettings(currentLayer, profileContext, runtimeSource),
        }),
      ]);

      markSaved();
    } catch (error) {
      markError(error);
    }
  }, [connected, markError, markSaved, markSaving, queryClient, selectedKeyIndexes]);

  // ── UI ──

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
        <Badge variant="secondary" className="h-8 px-2 text-xs">
          {selectedKeyIndexes.length} selected
        </Badge>
      </div>
      <div className="flex items-center gap-2">
        <AutosaveStatus state={saveState} />
        <Button
          variant="destructive"
          size="sm"
          className="h-8 gap-1.5"
          disabled={noSelection || !connected}
          onClick={() => void resetSelectedPerformance()}
        >
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
          onButtonClick={() => { }}
          showLayerSelector={false}
          showRotary={false}
          loading={keyboardPreviewLoading}
          keyLegendSlotsMap={keyLegendSlotsMap}
          keyLegendClassName="text-[9px] leading-[1.05]"
        />
      }
      menubar={menubar}
    >
      <div className="flex flex-col gap-4">
        {noSelection ? (
          <div className="flex flex-col items-center justify-center py-16 text-center border rounded-lg bg-muted/20 border-dashed">
            <div className="flex size-14 items-center justify-center rounded-full bg-muted/50 mb-4">
              <IconPointer className="size-6 text-muted-foreground" />
            </div>
            <h3 className="text-sm font-semibold text-foreground">Sélectionnez une touche</h3>
            <p className="text-xs text-muted-foreground max-w-[300px] mt-1.5">
              Cliquez sur une ou plusieurs touches du clavier interactif ci-dessus pour modifier leurs paramètres de performance individuellement.
            </p>
          </div>
        ) : keySettingsQ.isLoading ? (
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            <Card className="shadow-none border">
              <CardHeader className="pb-3 border-b">
                <CardTitle className="text-sm font-medium">Point d'activation</CardTitle>
                <CardDescription className="text-xs mt-0.5">Reglages d'activation et de relachement</CardDescription>
              </CardHeader>
              <CardContent className="pt-4 flex flex-col gap-3">
                {[0, 1].map((i) => <Skeleton key={i} className="h-9 w-full" />)}
              </CardContent>
            </Card>
            <Card className="shadow-none border">
              <CardHeader className="pb-3 border-b">
                <CardTitle className="text-sm font-medium">Rapid Trigger</CardTitle>
                <CardDescription className="text-xs mt-0.5">Reglages RT et options associees</CardDescription>
              </CardHeader>
              <CardContent className="pt-4 flex flex-col gap-3">
                {[0, 1, 2].map((i) => <Skeleton key={i} className="h-9 w-full" />)}
              </CardContent>
            </Card>
          </div>
        ) : !settings ? (
          <p className="text-sm text-muted-foreground">Could not load settings.</p>
        ) : (
          <div className="flex flex-col gap-4">
            {isMultiSelection && hasCompleteSelectedSettings && hasMixedValues && (
              <Card className="overflow-hidden border border-amber-500/30 bg-amber-500/5 dark:bg-amber-500/10">
                <CardContent className="p-4 flex items-center gap-4">
                  <div className="flex size-10 shrink-0 items-center justify-center rounded-full bg-amber-500/20">
                    <IconAlertTriangle className="size-5 text-amber-600 dark:text-amber-400" />
                  </div>
                  <div className="flex flex-col gap-1 text-sm">
                    <p className="font-semibold text-amber-800 dark:text-amber-300">
                      Paramètres hétérogènes détectés
                    </p>
                    <p className="text-amber-700/80 dark:text-amber-300/80">
                      Les {selectedKeyIndexes.length} touches sélectionnées ont des paramètres différents. La modification entraînera l'écrasement des anciens paramètres.
                    </p>
                  </div>
                </CardContent>
              </Card>
            )}

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <Card className="shadow-none border">
                <CardHeader className="pb-3 border-b">
                  <CardTitle className="text-sm font-medium">Point d'activation</CardTitle>
                  <CardDescription className="text-xs mt-0.5">Reglages d'activation et de relachement</CardDescription>
                </CardHeader>
                <CardContent className="pt-4 flex flex-col gap-4">
                  <DistanceSlider
                    label="Actuation Point"
                    value={settings.actuation_point_mm}
                    onLiveChange={v => liveKeyUpdate({ actuation_point_mm: v })}
                    onChange={v => commitKey({ actuation_point_mm: v })}
                    disabled={!connected}
                  />
                  {isMultiSelection && mixedValues.actuation_point_mm && (
                    <p className="-mt-2 text-xs text-muted-foreground">Les valeurs actuelles diffèrent. Bouger le slider écrasera toutes les touches sélectionnées.</p>
                  )}
                  <DistanceSlider
                    label="Release Point"
                    value={settings.release_point_mm}
                    onLiveChange={v => liveKeyUpdate({ release_point_mm: v })}
                    onChange={v => commitKey({ release_point_mm: v })}
                    disabled={!connected}
                  />
                  {isMultiSelection && mixedValues.release_point_mm && (
                    <p className="-mt-2 text-xs text-muted-foreground">Les valeurs actuelles diffèrent. Bouger le slider écrasera toutes les touches sélectionnées.</p>
                  )}
                </CardContent>
              </Card>

              <Card className="shadow-none border">
                <CardHeader className="pb-3 border-b">
                  <div className="flex items-start justify-between gap-3">
                    <div>
                      <CardTitle className="text-sm font-medium">Rapid Trigger</CardTitle>
                      <CardDescription className="text-xs mt-0.5">Reglages RT et options associees</CardDescription>
                    </div>
                    <Switch
                      checked={rapidTriggerSectionEnabled}
                      disabled={!connected}
                      onCheckedChange={(enabled) => commitKey({ rapid_trigger_enabled: enabled })}
                    />
                  </div>
                </CardHeader>
                <CardContent className="pt-4 flex flex-col gap-4">
                  <DistanceSlider
                    label="RT Press Sensitivity"
                    value={settings.rapid_trigger_press}
                    min={0.01} max={2.55} step={0.01}
                    displayDecimals={2}
                    onLiveChange={v => liveKeyUpdate({ rapid_trigger_press: v })}
                    onChange={v => commitKey({ rapid_trigger_press: v })}
                    disabled={!connected || !rapidTriggerSectionEnabled}
                  />
                  {isMultiSelection && mixedValues.rapid_trigger_press && (
                    <p className="-mt-2 text-xs text-muted-foreground">Les valeurs actuelles diffèrent. Bouger le slider écrasera toutes les touches sélectionnées.</p>
                  )}
                  <FormRow
                    label="Use separate release sensitivity"
                    description="If disabled, release sensitivity follows press sensitivity"
                  >
                    <Checkbox
                      checked={useSeparateReleaseSensitivity}
                      disabled={!connected || !rapidTriggerSectionEnabled}
                      onCheckedChange={(checked) => {
                        const enabled = Boolean(checked);
                        setUseSeparateReleaseSensitivity(enabled);

                        if (!enabled) {
                          commitKey({}, { enforceLinkedRapidSensitivity: true });
                        }
                      }}
                    />
                  </FormRow>
                  {isMultiSelection && mixedValues.separate_release_sensitivity && (
                    <p className="-mt-2 text-xs text-muted-foreground">Les valeurs actuelles diffèrent. Changer cette option écrasera toutes les touches sélectionnées.</p>
                  )}

                  <DistanceSlider
                    label="RT Release Sensitivity"
                    value={settings.rapid_trigger_release}
                    min={0.01} max={2.55} step={0.01}
                    displayDecimals={2}
                    onLiveChange={v => liveKeyUpdate({ rapid_trigger_release: v })}
                    onChange={v => commitKey({ rapid_trigger_release: v })}
                    disabled={!connected || !rapidTriggerSectionEnabled || !useSeparateReleaseSensitivity}
                  />
                  {isMultiSelection && mixedValues.rapid_trigger_release && (
                    <p className="-mt-2 text-xs text-muted-foreground">Les valeurs actuelles diffèrent. Bouger le slider écrasera toutes les touches sélectionnées.</p>
                  )}

                  <FormRow label="Continuous RT" description="Track past bottom">
                    <Switch
                      checked={settings.continuous_rapid_trigger}
                      disabled={!connected || !rapidTriggerSectionEnabled}
                      onCheckedChange={v => commitKey({ continuous_rapid_trigger: v })}
                    />
                  </FormRow>
                  {isMultiSelection && mixedValues.continuous_rapid_trigger && (
                    <p className="-mt-2 text-xs text-muted-foreground">Les valeurs actuelles diffèrent. Changer ce switch écrasera toutes les touches sélectionnées.</p>
                  )}
                </CardContent>
              </Card>
            </div>
          </div>
        )}

        {noSelection && (
          <SectionCard title="Global Performance">
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <div className="flex flex-col gap-4">
                <FormRow label="Input Filter" description="ADC noise suppression">
                  <Switch
                    checked={filterEnabledQ.data ?? false}
                    disabled={!connected}
                    onCheckedChange={v => filterMutation.mutate(v)}
                  />
                </FormRow>
                {filterEnabledQ.data && filterParamsQ.data && (
                  <>
                    <div className="grid gap-2">
                      <span className="text-sm font-medium">Noise Band</span>
                      <CommitSlider
                        min={1} max={255} step={1}
                        value={filterParamsQ.data.noise_band}
                        onLiveChange={v => liveFilterParams({ ...filterParamsQ.data!, noise_band: v })}
                        onCommit={v => filterParamsMutation.mutate({ ...filterParamsQ.data!, noise_band: v })}
                        disabled={!connected}
                      />
                    </div>
                    <div className="grid gap-2">
                      <span className="text-sm font-medium">Alpha Min Denom</span>
                      <CommitSlider
                        min={1} max={255} step={1}
                        value={filterParamsQ.data.alpha_min_denom}
                        onLiveChange={v => liveFilterParams({ ...filterParamsQ.data!, alpha_min_denom: v })}
                        onCommit={v => filterParamsMutation.mutate({ ...filterParamsQ.data!, alpha_min_denom: v })}
                        disabled={!connected}
                      />
                    </div>
                    <div className="grid gap-2">
                      <span className="text-sm font-medium">Alpha Max Denom</span>
                      <CommitSlider
                        min={1} max={255} step={1}
                        value={filterParamsQ.data.alpha_max_denom}
                        onLiveChange={v => liveFilterParams({ ...filterParamsQ.data!, alpha_max_denom: v })}
                        onCommit={v => filterParamsMutation.mutate({ ...filterParamsQ.data!, alpha_max_denom: v })}
                        disabled={!connected}
                      />
                    </div>
                  </>
                )}
              </div>
              <div className="flex flex-col gap-4">
                <div className="grid gap-2">
                  <span className="text-sm font-medium">Advanced Tick Rate</span>
                  <CommitSlider
                    min={1} max={100} step={1}
                    value={tickRateQ.data ?? 1}
                    onLiveChange={v => liveTickRate(v)}
                    onCommit={v => tickMutation.mutate(v)}
                    disabled={!connected}
                  />
                </div>
                <FormRow
                  label="Chatter Guard"
                  description="Anti-chatter gate to block rapid bounce after trigger"
                >
                  <Switch
                    checked={chatterGuardValue.enabled}
                    disabled={!connected || triggerChatterGuardQ.data == null}
                    onCheckedChange={(enabled) => commitChatterGuard({ enabled })}
                  />
                </FormRow>
                <div className="grid gap-2">
                  <span className="text-sm font-medium">Chatter Guard Duration (ms)</span>
                  <CommitSlider
                    min={0}
                    max={TRIGGER_CHATTER_GUARD_MAX_MS}
                    step={1}
                    value={chatterGuardValue.duration_ms}
                    onLiveChange={() => {}}
                    onCommit={(duration) => commitChatterGuard({ duration_ms: duration })}
                    disabled={!connected || triggerChatterGuardQ.data == null}
                  />
                </div>
              </div>
            </div>
          </SectionCard>
        )}
      </div>
    </KeyboardEditor>
  );
}
