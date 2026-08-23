import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { useKeyboardPreviewLegends } from "@/hooks/use-keyboard-preview-legends";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { DistanceSlider } from "@/components/distance-slider";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow, FormRows } from "@/components/shared/SectionCard";
import { PageSection } from "@/components/shared/PageLayout";
import { EmptyState } from "@/components/shared/EmptyState";
import { SliderField } from "@/components/shared/SliderField";
import { Toolbar, ToolbarDivider, ToolbarStat } from "@/components/shared/Toolbar";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Switch } from "@/components/ui/switch";
import { Skeleton } from "@/components/ui/skeleton";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useProfileStore } from "@/stores/profileStore";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type KeySettings } from "@/lib/kbhe/device";
import { requireDeviceSuccess } from "@/lib/kbhe/mutation-result";
import {
  patchActiveAppProfileAdvancedTickRate,
  patchActiveAppProfileFilterEnabled,
  patchActiveAppProfileFilterParams,
  patchActiveAppProfileKeySettings,
  patchActiveAppProfileTriggerChatterGuard,
} from "@/lib/kbhe/profile-snapshot-store";
import {
  KEY_COUNT,
  TRIGGER_CHATTER_GUARD_MAX_MS,
  TRIGGER_CHATTER_GUARD_RECOMMENDED_MS,
} from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import {
  IconAlertTriangle,
  IconArrowBarToDown,
  IconBolt,
  IconClockBolt,
  IconSelectAll,
  IconDeselect,
  IconRestore,
  IconPointer,
  IconWaveSine,
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
  const {
    keyLegendSlotsMap,
    keyLegendOverlayMap,
    isLoading: keyboardPreviewLoading,
  } = useKeyboardPreviewLegends();
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

    const nextSettings = {
      ...settings,
      ...effectivePatch,
      profile_index: profileContext,
      layer_index: currentLayer,
    };
    const ok = await kbheDevice.setKeySettingsExtended(keyIndex, nextSettings);
    if (ok) {
      patchActiveAppProfileKeySettings(nextSettings);
    }
  });

  const liveFilterParams = useThrottledCall(async (params: FilterParams) => {
    const ok = await kbheDevice.setFilterParams(params.noise_band, params.alpha_min_denom, params.alpha_max_denom);
    if (ok) {
      patchActiveAppProfileFilterParams(params);
    }
  });

  const liveTickRate = useThrottledCall(async (v: number) => {
    const ok = await kbheDevice.setAdvancedTickRate(v);
    if (ok) {
      patchActiveAppProfileAdvancedTickRate(v);
    }
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
          // Read after earlier scoped mutations have settled. Reusing the
          // render-time `settings` object here can overwrite a just-committed
          // field when two controls are changed before React re-renders.
          const base = await kbheDevice.getKeySettings(
            targetKeyIndex,
            profileContext,
            currentLayer,
          );

          if (!base) {
            throw new Error(`Unable to load key settings for key ${targetKeyIndex}`);
          }

          const effectivePatch = enforceLinkedRapidSensitivity
            ? {
                ...patch,
                rapid_trigger_release: patch.rapid_trigger_press ?? base.rapid_trigger_press,
              }
            : patch;

          const nextSettings = {
            ...base,
            ...effectivePatch,
            profile_index: profileContext,
            layer_index: currentLayer,
          };

          const ok = await kbheDevice.setKeySettingsExtended(targetKeyIndex, nextSettings);
          if (!ok) {
            throw new Error(`Unable to update key settings for key ${targetKeyIndex}`);
          }
          patchActiveAppProfileKeySettings(nextSettings);
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
    mutationFn: async (v) => {
      markSaving();
      const ok = await kbheDevice.setFilterEnabled(v);
      requireDeviceSuccess(ok, "input filter setting");
      patchActiveAppProfileFilterEnabled(v);
      return ok;
    },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: () => markSaved(),
    onError: () => markError(),
  });

  const filterParamsDesiredRef = useRef<FilterParams | null>(null);
  const filterParamsMutation = useOptimisticMutation<FilterParams, FilterParams, boolean>({
    queryKey: queryKeys.device.filterParams(),
    mutationFn: async (p) => {
      markSaving();
      const ok = await kbheDevice.setFilterParams(p.noise_band, p.alpha_min_denom, p.alpha_max_denom);
      requireDeviceSuccess(ok, "input filter parameters");
      patchActiveAppProfileFilterParams(p);
      return ok;
    },
    optimisticUpdate: (_cur, p) => p,
    onSuccess: () => markSaved(),
    onError: () => {
      filterParamsDesiredRef.current = null;
      markError();
    },
  });

  const commitFilterParams = useCallback((patch: Partial<FilterParams>) => {
    const base = filterParamsDesiredRef.current ?? filterParamsQ.data;
    if (!base) return;
    const next = { ...base, ...patch };
    filterParamsDesiredRef.current = next;
    filterParamsMutation.mutate(next);
  }, [filterParamsMutation, filterParamsQ.data]);

  useEffect(() => {
    if (!filterParamsMutation.isPending) {
      filterParamsDesiredRef.current = filterParamsQ.data ?? null;
    }
  }, [filterParamsMutation.isPending, filterParamsQ.data]);

  const tickMutation = useOptimisticMutation<number, number, boolean>({
    queryKey: queryKeys.device.advancedTickRate(),
    mutationFn: async (v) => {
      markSaving();
      const ok = await kbheDevice.setAdvancedTickRate(v);
      requireDeviceSuccess(ok, "advanced tick rate");
      patchActiveAppProfileAdvancedTickRate(v);
      return ok;
    },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: () => markSaved(),
    onError: () => markError(),
  });

  const triggerChatterGuardMutation = useOptimisticMutation<
    { enabled: boolean; duration_ms: number } | null,
    { enabled: boolean; duration_ms: number },
    boolean
  >({
    queryKey: queryKeys.device.triggerChatterGuard(),
    mutationFn: async (next) => {
      markSaving();
      const ok = await kbheDevice.setTriggerChatterGuard(next.enabled, next.duration_ms);
      requireDeviceSuccess(ok, "trigger chatter guard");
      return ok;
    },
    optimisticUpdate: (_cur, next) => next,
    onSuccess: (_ok, next) => {
      patchActiveAppProfileTriggerChatterGuard(next);
      markSaved();
    },
    onError: () => markError(),
  });

  const chatterGuardValue = useMemo(
    () => triggerChatterGuardQ.data ?? { enabled: false, duration_ms: 0 },
    [triggerChatterGuardQ.data],
  );
  const chatterGuardDesiredRef = useRef(chatterGuardValue);

  const commitChatterGuard = useCallback(
    (patch: Partial<{ enabled: boolean; duration_ms: number }>) => {
      const next = { ...chatterGuardDesiredRef.current, ...patch };
      chatterGuardDesiredRef.current = next;
      triggerChatterGuardMutation.mutate(next);
    },
    [triggerChatterGuardMutation],
  );

  useEffect(() => {
    if (!triggerChatterGuardMutation.isPending) {
      chatterGuardDesiredRef.current = chatterGuardValue;
    }
  }, [chatterGuardValue, triggerChatterGuardMutation.isPending]);

  // Merge a partial patch with the current full settings and commit.
  function commitKey(
    patch: Partial<KeySettings>,
    options?: { enforceLinkedRapidSensitivity?: boolean },
  ) {
    if (selectedKeyIndexes.length === 0) return;

    const enforceLinkedRapidSensitivity =
      options?.enforceLinkedRapidSensitivity
      ?? (!useSeparateReleaseSensitivity && isRapidTriggerPatch(patch));

    const variables = {
      patch,
      keyIndexes: [...selectedKeyIndexes],
      enforceLinkedRapidSensitivity,
    };
    void liveKeyUpdate.cancelAndWait().then(() => keyMutation.mutate(variables));
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
          const nextSettings = await kbheDevice.getKeySettings(targetKeyIndex, profileContext, currentLayer);
          if (nextSettings) {
            patchActiveAppProfileKeySettings(nextSettings);
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
  }, [
    connected,
    currentLayer,
    markError,
    markSaved,
    markSaving,
    profileContext,
    queryClient,
    runtimeSource,
    selectedKeyIndexes,
  ]);

  // ── UI ──

  const menubar = (
    <Toolbar
      left={
        <>
          <ToolbarStat
            label="Selected"
            value={`${selectedKeyIndexes.length} ${selectedKeyIndexes.length === 1 ? "key" : "keys"}`}
            tone={selectedKeyIndexes.length > 0 ? "active" : "default"}
          />
          <ToolbarDivider />
          <Button variant="ghost" size="sm" onClick={selectAll}>
            <IconSelectAll className="size-4" />
            Select all
          </Button>
          <Button
            variant="ghost"
            size="sm"
            disabled={noSelection}
            onClick={clearSelection}
          >
            <IconDeselect className="size-4" />
            Clear
          </Button>
        </>
      }
      right={
        <>
          <AutosaveStatus state={saveState} />
          <Button
            variant="destructive"
            size="sm"
            disabled={noSelection || !connected}
            onClick={() => void resetSelectedPerformance()}
          >
            <IconRestore className="size-4" />
            Reset selected
          </Button>
        </>
      }
    />
  );

  const mixedNotice = (mixed: boolean | undefined) =>
    isMultiSelection && mixed ? (
      <Badge
        variant="outline"
        className="border-warning/40 bg-warning/10 text-warning"
        title="The selected keys currently hold different values for this setting"
      >
        Mixed
      </Badge>
    ) : null;

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
          keyLegendOverlayMap={keyLegendOverlayMap}
          keyLegendClassName="text-[9px] leading-[1.05]"
        />
      }
      menubar={menubar}
    >
      <div className="flex flex-col gap-6">
        <PageSection
          title="Per-key settings"
          description={
            noSelection
              ? "Pick one or more keys in the preview above to edit them."
              : `Editing ${selectedKeyIndexes.length} ${selectedKeyIndexes.length === 1 ? "key" : "keys"}.`
          }
        >
          {noSelection ? (
            <EmptyState
              icon={<IconPointer />}
              title="No keys selected"
              description="Click a key in the keyboard above — or drag across several — to change its actuation point and rapid trigger behaviour."
              action={
                <Button variant="outline" size="sm" onClick={selectAll}>
                  <IconSelectAll className="size-4" />
                  Select all keys
                </Button>
              }
            />
          ) : keySettingsQ.isLoading ? (
            <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
              <SectionCard title="Actuation" description="Loading…">
                <div className="flex flex-col gap-4">
                  {[0, 1].map((i) => <Skeleton key={i} className="h-12 w-full" />)}
                </div>
              </SectionCard>
              <SectionCard title="Rapid trigger" description="Loading…">
                <div className="flex flex-col gap-4">
                  {[0, 1, 2].map((i) => <Skeleton key={i} className="h-12 w-full" />)}
                </div>
              </SectionCard>
            </div>
          ) : !settings ? (
            <EmptyState
              icon={<IconAlertTriangle />}
              title="Could not read these keys"
              description="The keyboard did not answer the settings request. Reselect the keys or reconnect the device."
            />
          ) : (
            <div className="flex flex-col gap-4">
              {isMultiSelection && hasCompleteSelectedSettings && hasMixedValues && (
                <div className="flex items-start gap-3 rounded-xl border border-warning/30 bg-warning/8 px-4 py-3">
                  <IconAlertTriangle className="mt-px size-4 shrink-0 text-warning" />
                  <div className="min-w-0 text-xs leading-relaxed">
                    <p className="text-sm font-medium text-foreground">
                      These keys do not share the same settings
                    </p>
                    <p className="mt-0.5 text-muted-foreground">
                      Controls marked <span className="font-medium text-warning">Mixed</span> hold
                      different values across the {selectedKeyIndexes.length} selected keys.
                      Changing one overwrites all of them.
                    </p>
                  </div>
                </div>
              )}

              <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
                <SectionCard
                  title="Actuation"
                  description="How far the key must travel before it registers."
                  icon={<IconArrowBarToDown />}
                >
                  <div className="flex flex-col gap-5">
                    <DistanceSlider
                      label="Actuation point"
                      value={settings.actuation_point_mm}
                      onLiveChange={v => liveKeyUpdate({ actuation_point_mm: v })}
                      onChange={v => commitKey({ actuation_point_mm: v })}
                      disabled={!connected}
                      labelRight={mixedNotice(mixedValues.actuation_point_mm)}
                      description="Depth at which a press is reported."
                    />
                    <DistanceSlider
                      label="Release point"
                      value={settings.release_point_mm}
                      onLiveChange={v => liveKeyUpdate({ release_point_mm: v })}
                      onChange={v => commitKey({ release_point_mm: v })}
                      disabled={!connected}
                      labelRight={mixedNotice(mixedValues.release_point_mm)}
                      description="Depth at which the key is considered released."
                    />
                  </div>
                </SectionCard>

                <SectionCard
                  title="Rapid trigger"
                  description="Re-arm the key from wherever it currently sits, instead of a fixed point."
                  icon={<IconBolt />}
                  headerRight={
                    <Switch
                      checked={rapidTriggerSectionEnabled}
                      disabled={!connected}
                      onCheckedChange={(enabled) => commitKey({ rapid_trigger_enabled: enabled })}
                      aria-label="Enable rapid trigger"
                    />
                  }
                >
                  {!rapidTriggerSectionEnabled ? (
                    <p className="text-xs leading-relaxed text-muted-foreground">
                      Rapid trigger is off for {isMultiSelection ? "these keys" : "this key"}.
                      Turn it on to set press and release sensitivity.
                    </p>
                  ) : (
                    <div className="flex flex-col gap-5">
                      <DistanceSlider
                        label="Press sensitivity"
                        value={settings.rapid_trigger_press}
                        min={0.01} max={2.55} step={0.01}
                        displayDecimals={2}
                        onLiveChange={v => liveKeyUpdate({ rapid_trigger_press: v })}
                        onChange={v => commitKey({ rapid_trigger_press: v })}
                        disabled={!connected}
                        labelRight={mixedNotice(mixedValues.rapid_trigger_press)}
                        description="Upward travel needed to re-arm the key."
                      />

                      <FormRows>
                        <FormRow
                          label="Separate release sensitivity"
                          description="When off, release sensitivity mirrors press sensitivity."
                        >
                          {mixedNotice(mixedValues.separate_release_sensitivity)}
                          <Checkbox
                            checked={useSeparateReleaseSensitivity}
                            disabled={!connected}
                            onCheckedChange={(checked) => {
                              const enabled = Boolean(checked);
                              setUseSeparateReleaseSensitivity(enabled);
                              if (!enabled) {
                                commitKey({}, { enforceLinkedRapidSensitivity: true });
                              }
                            }}
                          />
                        </FormRow>
                      </FormRows>

                      {useSeparateReleaseSensitivity && (
                        <DistanceSlider
                          label="Release sensitivity"
                          value={settings.rapid_trigger_release}
                          min={0.01} max={2.55} step={0.01}
                          displayDecimals={2}
                          onLiveChange={v => liveKeyUpdate({ rapid_trigger_release: v })}
                          onChange={v => commitKey({ rapid_trigger_release: v })}
                          disabled={!connected}
                          labelRight={mixedNotice(mixedValues.rapid_trigger_release)}
                          description="Downward travel needed to fire again."
                        />
                      )}

                      <FormRows>
                        <FormRow
                          label="Continuous rapid trigger"
                          description="Keep tracking the key past the bottom of its travel."
                        >
                          {mixedNotice(mixedValues.continuous_rapid_trigger)}
                          <Switch
                            checked={settings.continuous_rapid_trigger}
                            disabled={!connected}
                            onCheckedChange={v => commitKey({ continuous_rapid_trigger: v })}
                          />
                        </FormRow>
                      </FormRows>
                    </div>
                  )}
                </SectionCard>
              </div>
            </div>
          )}
        </PageSection>

        <PageSection
          title="Whole-keyboard settings"
          description="These apply to every key, regardless of what is selected above."
        >
          <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
            <SectionCard
              title="Input filter"
              description="Smooths analog noise coming off the Hall sensors."
              icon={<IconWaveSine />}
              headerRight={
                <Switch
                  checked={filterEnabledQ.data ?? false}
                  disabled={!connected}
                  onCheckedChange={v => filterMutation.mutate(v)}
                  aria-label="Enable input filter"
                />
              }
            >
              {!filterEnabledQ.data || !filterParamsQ.data ? (
                <p className="text-xs leading-relaxed text-muted-foreground">
                  Filtering is off. Enable it if resting keys report small random
                  movements; the cost is a fraction of a millisecond of extra latency.
                </p>
              ) : (
                <div className="flex flex-col gap-5">
                  <SliderField
                    label="Noise band"
                    description="Movement smaller than this is treated as sensor noise."
                    min={1} max={255} step={1}
                    value={filterParamsQ.data.noise_band}
                    onLiveChange={v => liveFilterParams({ ...filterParamsQ.data!, noise_band: v })}
                    onCommit={v => {
                      void liveFilterParams.cancelAndWait().then(() => {
                        commitFilterParams({ noise_band: v });
                      });
                    }}
                    disabled={!connected}
                  />
                  <SliderField
                    label="Minimum smoothing"
                    description="Filter strength while the key is moving fast. Lower reacts quicker."
                    min={1} max={255} step={1}
                    value={filterParamsQ.data.alpha_min_denom}
                    onLiveChange={v => liveFilterParams({ ...filterParamsQ.data!, alpha_min_denom: v })}
                    onCommit={v => {
                      void liveFilterParams.cancelAndWait().then(() => {
                        commitFilterParams({ alpha_min_denom: v });
                      });
                    }}
                    disabled={!connected}
                  />
                  <SliderField
                    label="Maximum smoothing"
                    description="Filter strength while the key is nearly still. Higher is steadier."
                    min={1} max={255} step={1}
                    value={filterParamsQ.data.alpha_max_denom}
                    onLiveChange={v => liveFilterParams({ ...filterParamsQ.data!, alpha_max_denom: v })}
                    onCommit={v => {
                      void liveFilterParams.cancelAndWait().then(() => {
                        commitFilterParams({ alpha_max_denom: v });
                      });
                    }}
                    disabled={!connected}
                  />
                </div>
              )}
            </SectionCard>

            <SectionCard
              title="Timing"
              description="Scan cadence and debounce behaviour."
              icon={<IconClockBolt />}
            >
              <div className="flex flex-col gap-5">
                <SliderField
                  label="Advanced tick rate"
                  description="How often advanced-key logic re-evaluates, in scan ticks."
                  min={1} max={100} step={1}
                  value={tickRateQ.data ?? 1}
                  onLiveChange={v => liveTickRate(v)}
                  onCommit={v => {
                    void liveTickRate.cancelAndWait().then(() => tickMutation.mutate(v));
                  }}
                  disabled={!connected}
                />

                <FormRows>
                  <FormRow
                    label="Chatter guard"
                    description="Only accept a press or release once the new state has held steady."
                  >
                    <Switch
                      checked={chatterGuardValue.enabled}
                      disabled={!connected || triggerChatterGuardQ.data == null}
                      onCheckedChange={(enabled) => commitChatterGuard({
                        enabled,
                        duration_ms: enabled && chatterGuardValue.duration_ms === 0
                          ? TRIGGER_CHATTER_GUARD_RECOMMENDED_MS
                          : chatterGuardValue.duration_ms,
                      })}
                    />
                  </FormRow>
                </FormRows>

                {chatterGuardValue.enabled && (
                  <SliderField
                    label="Guard window"
                    description={`How long the state must hold. ${TRIGGER_CHATTER_GUARD_RECOMMENDED_MS} ms is the recommended starting point.`}
                    min={1}
                    max={TRIGGER_CHATTER_GUARD_MAX_MS}
                    step={1}
                    unit="ms"
                    value={chatterGuardValue.duration_ms}
                    onCommit={(duration) => commitChatterGuard({ duration_ms: duration })}
                    disabled={!connected || triggerChatterGuardQ.data == null}
                  />
                )}
              </div>
            </SectionCard>
          </div>
        </PageSection>
      </div>
    </KeyboardEditor>
  );
}
