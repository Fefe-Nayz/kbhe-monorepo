import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useKeyboardPreviewLegends } from "@/hooks/use-keyboard-preview-legends";
import { useOSKeycapLegend } from "@/hooks/use-os-layout";
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
import { CommitSlider } from "@/components/ui/commit-slider";
import { Switch } from "@/components/ui/switch";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Select, SelectContent, SelectGroup, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type KeySettings } from "@/lib/kbhe/device";
import { KEY_BEHAVIORS, HID_KEYCODE_NAMES, SOCD_RESOLUTIONS, KEY_COUNT } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { cn, selectItems } from "@/lib/utils";
import {
  IconPlus,
  IconToggleLeft,
  IconHandClick,
  IconArrowsSplit2,
  IconRoute,
} from "@tabler/icons-react";

type AdvancedMenu = "socd" | "tap-hold" | "toggle" | "dynamic";

type AssignState =
  | null
  | { kind: "socd"; slot: 1 | 2 }
  | { kind: "behavior"; menu: Exclude<AdvancedMenu, "socd"> };

type HoveredAdvancedItem =
  | null
  | { kind: "socd"; a: number; b: number }
  | { kind: "behavior"; keyIndex: number };

const ADVANCED_MENU_INFO: Array<{
  id: AdvancedMenu;
  label: string;
  description: string;
  icon: typeof IconRoute;
}> = [
  {
    id: "socd",
    label: "SOCD",
    description: "Resolve opposite directions between two keys",
    icon: IconRoute,
  },
  {
    id: "tap-hold",
    label: "Tap-Hold",
    description: "Tap sends one key, hold sends another",
    icon: IconHandClick,
  },
  {
    id: "toggle",
    label: "Toggle",
    description: "Press once to hold, press again to release",
    icon: IconToggleLeft,
  },
  {
    id: "dynamic",
    label: "Dynamic",
    description: "Trigger actions based on travel depth",
    icon: IconArrowsSplit2,
  },
];

const EMPTY_ADVANCED_SLOTS: Array<ReactNode | undefined> = Array.from({ length: 12 }, () => "");

function iconForAdvancedMenu(menu: AdvancedMenu) {
  switch (menu) {
    case "socd": return IconRoute;
    case "tap-hold": return IconHandClick;
    case "toggle": return IconToggleLeft;
    case "dynamic": return IconArrowsSplit2;
  }
}

function parseKeyIndexFromId(id: string): number | null {
  if (!id.startsWith("key-")) return null;
  const parsed = Number.parseInt(id.replace("key-", ""), 10);
  return Number.isFinite(parsed) ? parsed : null;
}

function behaviorFromMenu(menu: Exclude<AdvancedMenu, "socd">): number {
  switch (menu) {
    case "tap-hold":
      return KEY_BEHAVIORS["Tap-Hold"];
    case "toggle":
      return KEY_BEHAVIORS.Toggle;
    case "dynamic":
      return KEY_BEHAVIORS["Dynamic Mapping"];
  }
}

function menuFromBehavior(behavior: number): Exclude<AdvancedMenu, "socd"> | null {
  if (behavior === KEY_BEHAVIORS["Tap-Hold"]) return "tap-hold";
  if (behavior === KEY_BEHAVIORS.Toggle) return "toggle";
  if (behavior === KEY_BEHAVIORS["Dynamic Mapping"]) return "dynamic";
  return null;
}

async function fetchAllDetailedKeySettings(): Promise<KeySettings[]> {
  const settings: KeySettings[] = [];
  const batchSize = 8;

  for (let start = 0; start < KEY_COUNT; start += batchSize) {
    const end = Math.min(start + batchSize, KEY_COUNT);
    const requests = Array.from({ length: end - start }, (_, i) => kbheDevice.getKeySettings(start + i));
    const results = await Promise.all(requests);
    for (const item of results) {
      if (item) settings.push(item);
    }
  }

  return settings;
}

export default function AdvancedKeys() {
  const clearSelection = useKeyboardStore((s) => s.clearSelection);
  const setSelectedKeys = useKeyboardStore((s) => s.setSelectedKeys);
  const currentLayer = useKeyboardStore((s) => s.currentLayer);
  const setCurrentLayer = useKeyboardStore((s) => s.setCurrentLayer);
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const resolveKeycapLegend = useOSKeycapLegend();
  const { keyLegendSlotsMap, isLoading: keyboardPreviewLoading } = useKeyboardPreviewLegends();
  const { saveState, markSaving, markSaved, markError } = useAutosave();
  const queryClient = useQueryClient();

  const [activeMenu, setActiveMenu] = useState<AdvancedMenu>("socd");
  const [panelMode, setPanelMode] = useState<"overview" | "configure">("overview");
  const [assignState, setAssignState] = useState<AssignState>(null);
  const [activeKeyIndex, setActiveKeyIndex] = useState<number | null>(null);
  const [hoveredAdvancedItem, setHoveredAdvancedItem] = useState<HoveredAdvancedItem>(null);
  const [pendingSocd, setPendingSocd] = useState<{ touch1: number | null; touch2: number | null }>({
    touch1: null,
    touch2: null,
  });
  const [selectedDynamicZone, setSelectedDynamicZone] = useState(0);

  const allSettingsQ = useQuery({
    queryKey: queryKeys.keymap.allSettings(),
    queryFn: fetchAllDetailedKeySettings,
    enabled: connected,
    staleTime: 15_000,
  });

  const activeSettingsQ = useQuery({
    queryKey: queryKeys.keymap.keySettings(activeKeyIndex ?? -1),
    queryFn: () => activeKeyIndex != null ? kbheDevice.getKeySettings(activeKeyIndex) : null,
    enabled: connected && activeKeyIndex != null,
  });

  const tickRateQ = useQuery({
    queryKey: queryKeys.device.advancedTickRate(),
    queryFn: () => kbheDevice.getAdvancedTickRate(),
    enabled: connected,
  });

  const settingsByIndex = useMemo(() => {
    const next = new Map<number, KeySettings>();
    for (const entry of allSettingsQ.data ?? []) {
      next.set(entry.key_index, entry);
    }
    return next;
  }, [allSettingsQ.data]);

  const settings = useMemo(() => {
    if (activeSettingsQ.data) return activeSettingsQ.data;
    if (activeKeyIndex == null) return null;
    return settingsByIndex.get(activeKeyIndex) ?? null;
  }, [activeKeyIndex, activeSettingsQ.data, settingsByIndex]);

  const advancedMenuByIndex = useMemo(() => {
    const next = new Map<number, AdvancedMenu>();

    for (const item of allSettingsQ.data ?? []) {
      if (item.socd_pair != null) {
        next.set(item.key_index, "socd");
        continue;
      }

      const menu = menuFromBehavior(item.behavior_mode);
      if (menu) {
        next.set(item.key_index, menu);
      }
    }

    return next;
  }, [allSettingsQ.data]);

  const getBaseSettingsForKey = useCallback(async (keyIndex: number): Promise<KeySettings | null> => {
    if (activeKeyIndex === keyIndex && settings) {
      return settings;
    }
    return settingsByIndex.get(keyIndex) ?? await kbheDevice.getKeySettings(keyIndex);
  }, [activeKeyIndex, settings, settingsByIndex]);

  const writeKeyPatch = useCallback(async (keyIndex: number, patch: Partial<KeySettings>) => {
    const base = await getBaseSettingsForKey(keyIndex);
    if (!base) {
      throw new Error(`Unable to load key settings for key ${keyIndex}`);
    }
    await kbheDevice.setKeySettingsExtended(keyIndex, { ...base, ...patch });
  }, [getBaseSettingsForKey]);

  const keyMutation = useOptimisticMutation<KeySettings | null, Partial<KeySettings>>({
    queryKey: queryKeys.keymap.keySettings(activeKeyIndex ?? -1),
    mutationFn: async (patch) => {
      if (activeKeyIndex == null) return;
      markSaving();
      await writeKeyPatch(activeKeyIndex, patch);
    },
    optimisticUpdate: (cur, patch) => cur ? { ...cur, ...patch } : cur ?? null,
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.keymap.allSettings() });
      markSaved();
    },
    onError: markError,
  });

  const updateKeyByIndex = useCallback(async (keyIndex: number, patch: Partial<KeySettings>) => {
    try {
      markSaving();
      await writeKeyPatch(keyIndex, patch);
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: queryKeys.keymap.keySettings(keyIndex) }),
        queryClient.invalidateQueries({ queryKey: queryKeys.keymap.allSettings() }),
      ]);
      markSaved();
    } catch (error) {
      markError(error);
    }
  }, [markError, markSaved, markSaving, queryClient, writeKeyPatch]);

  const assignBehaviorToKey = useCallback(async (
    keyIndex: number,
    menu: Exclude<AdvancedMenu, "socd">,
  ) => {
    const behavior = behaviorFromMenu(menu);

    if (menu === "toggle") {
      const base = await getBaseSettingsForKey(keyIndex);
      if (!base) {
        markError(new Error(`Unable to load key settings for key ${keyIndex}`));
        return;
      }

      await updateKeyByIndex(keyIndex, {
        behavior_mode: behavior,
        secondary_hid_keycode: base.hid_keycode,
      });
      return;
    }

    await updateKeyByIndex(keyIndex, { behavior_mode: behavior });
  }, [getBaseSettingsForKey, markError, updateKeyByIndex]);

  const upsertSocdPair = useCallback(async (touch1: number, touch2: number, resolution?: number) => {
    try {
      const [base1, base2] = await Promise.all([
        getBaseSettingsForKey(touch1),
        getBaseSettingsForKey(touch2),
      ]);

      if (!base1 || !base2) {
        throw new Error("Unable to load SOCD settings for selected keys");
      }

      const resolvedResolution = resolution ?? base1.socd_resolution ?? base2.socd_resolution ?? 0;

      markSaving();

      await Promise.all([
        writeKeyPatch(touch1, { socd_pair: touch2, socd_resolution: resolvedResolution }),
        writeKeyPatch(touch2, { socd_pair: touch1, socd_resolution: resolvedResolution }),
      ]);

      await Promise.all([
        queryClient.invalidateQueries({ queryKey: queryKeys.keymap.keySettings(touch1) }),
        queryClient.invalidateQueries({ queryKey: queryKeys.keymap.keySettings(touch2) }),
        queryClient.invalidateQueries({ queryKey: queryKeys.keymap.allSettings() }),
      ]);

      markSaved();
    } catch (error) {
      markError(error);
    }
  }, [getBaseSettingsForKey, markError, markSaved, markSaving, queryClient, writeKeyPatch]);

  const clearSocdPair = useCallback(async (touch1: number, touch2?: number | null) => {
    try {
      const targets = new Set<number>([touch1]);
      const base1 = await getBaseSettingsForKey(touch1);
      if (base1?.socd_pair != null) {
        targets.add(base1.socd_pair);
      }

      if (touch2 != null && touch2 !== touch1) {
        targets.add(touch2);
        const base2 = await getBaseSettingsForKey(touch2);
        if (base2?.socd_pair != null) {
          targets.add(base2.socd_pair);
        }
      }

      markSaving();

      await Promise.all(
        Array.from(targets).map((index) => writeKeyPatch(index, { socd_pair: 255 })),
      );

      await Promise.all([
        ...Array.from(targets).map((index) =>
          queryClient.invalidateQueries({ queryKey: queryKeys.keymap.keySettings(index) })),
        queryClient.invalidateQueries({ queryKey: queryKeys.keymap.allSettings() }),
      ]);

      markSaved();
    } catch (error) {
      markError(error);
    }
  }, [getBaseSettingsForKey, markError, markSaved, markSaving, queryClient, writeKeyPatch]);

  const closeConfigurePanel = useCallback(() => {
    setPanelMode("overview");
    setAssignState(null);
    setPendingSocd({ touch1: null, touch2: null });
    setActiveKeyIndex(null);
    setHoveredAdvancedItem(null);
    clearSelection();
  }, [clearSelection]);

  const handleSelectMenu = useCallback((menu: AdvancedMenu) => {
    setPanelMode("configure");
    setActiveMenu(menu);
    setActiveKeyIndex(null);
    setHoveredAdvancedItem(null);
    if (menu === "socd") {
      setAssignState({ kind: "socd", slot: 1 });
      setPendingSocd({ touch1: null, touch2: null });
      return;
    }
    setAssignState({ kind: "behavior", menu });
  }, []);

  const handleKeyboardClick = useCallback((ids: string[] | string) => {
    if (typeof ids !== "string") return;

    const clickedIndex = parseKeyIndexFromId(ids);
    if (clickedIndex == null) return;

    if (assignState == null) {
      const existingMenu = advancedMenuByIndex.get(clickedIndex);
      if (!existingMenu) return;

      setPanelMode("configure");
      setActiveMenu(existingMenu);
      setActiveKeyIndex(clickedIndex);
      setAssignState(null);
      setPendingSocd({ touch1: null, touch2: null });
      clearSelection();
      return;
    }

    if (assignState.kind === "behavior") {
      void assignBehaviorToKey(clickedIndex, assignState.menu);
      setActiveKeyIndex(clickedIndex);
      setAssignState(null);
      clearSelection();
      return;
    }

    if (assignState.slot === 1) {
      setPendingSocd({ touch1: clickedIndex, touch2: null });
      setActiveKeyIndex(clickedIndex);
      setAssignState({ kind: "socd", slot: 2 });
      clearSelection();
      return;
    }

    const touch1ForPair = pendingSocd.touch1 ?? activeKeyIndex;
    if (touch1ForPair == null || touch1ForPair === clickedIndex) return;

    void upsertSocdPair(touch1ForPair, clickedIndex);
    setPendingSocd({ touch1: touch1ForPair, touch2: clickedIndex });
    setActiveKeyIndex(touch1ForPair);
    setAssignState(null);
    clearSelection();
  }, [activeKeyIndex, assignBehaviorToKey, assignState, advancedMenuByIndex, clearSelection, pendingSocd.touch1, upsertSocdPair]);

  const keycodeDisplayName = useCallback((hidKeycode: number | null | undefined) => {
    if (hidKeycode == null) return "Not selected";
    const fallback = HID_KEYCODE_NAMES[hidKeycode] ?? `0x${hidKeycode.toString(16)}`;
    return resolveKeycapLegend(hidKeycode, fallback).text || fallback;
  }, [resolveKeycapLegend]);

  const activeKeycodeName = useCallback((index: number | null) => {
    if (index == null) return "Not selected";
    const source = index === activeKeyIndex ? settings : settingsByIndex.get(index);
    return keycodeDisplayName(source?.hid_keycode);
  }, [activeKeyIndex, keycodeDisplayName, settings, settingsByIndex]);

  const keyboardLegendSlotsMap = useMemo(() => {
    const next: Record<string, Array<ReactNode | undefined>> = {};

    for (const [keyId, slots] of Object.entries(keyLegendSlotsMap)) {
      next[keyId] = [...slots];
    }

    for (const [index, menu] of advancedMenuByIndex.entries()) {
      const Icon = iconForAdvancedMenu(menu);
      const slots: Array<ReactNode | undefined> = [...EMPTY_ADVANCED_SLOTS];
      slots[4] = <Icon className="size-3.5" />;
      next[`key-${index}`] = slots;
    }

    return next;
  }, [advancedMenuByIndex, keyLegendSlotsMap]);

  const keyboardKeyColorMap = useMemo(() => {
    const next: Record<string, string> = {};

    if (hoveredAdvancedItem?.kind === "socd") {
      next[`key-${hoveredAdvancedItem.a}`] = "#22c55e33";
      next[`key-${hoveredAdvancedItem.b}`] = "#22c55e33";
    }

    if (hoveredAdvancedItem?.kind === "behavior") {
      next[`key-${hoveredAdvancedItem.keyIndex}`] = "#22c55e33";
    }

    return next;
  }, [hoveredAdvancedItem]);

  const handleKeyboardHoverChange = useCallback((keyId: string | null) => {
    if (keyId == null) {
      setHoveredAdvancedItem(null);
      return;
    }

    const hoveredIndex = parseKeyIndexFromId(keyId);
    if (hoveredIndex == null) {
      setHoveredAdvancedItem(null);
      return;
    }

    const hoveredSettings = settingsByIndex.get(hoveredIndex);
    if (hoveredSettings?.socd_pair != null) {
      const a = Math.min(hoveredIndex, hoveredSettings.socd_pair);
      const b = Math.max(hoveredIndex, hoveredSettings.socd_pair);
      setHoveredAdvancedItem({ kind: "socd", a, b });
      return;
    }

    const menu = advancedMenuByIndex.get(hoveredIndex);
    if (menu && menu !== "socd") {
      setHoveredAdvancedItem({ kind: "behavior", keyIndex: hoveredIndex });
      return;
    }

    setHoveredAdvancedItem(null);
  }, [advancedMenuByIndex, settingsByIndex]);

  const socdTouch1 = pendingSocd.touch1 ?? (activeMenu === "socd" ? activeKeyIndex : null);
  const socdTouch2 = pendingSocd.touch2 ?? (activeMenu === "socd" ? settings?.socd_pair ?? null : null);

  const socdPairs = useMemo(() => {
    const seen = new Set<string>();
    const pairs: Array<{ a: number; b: number; resolution: number }> = [];

    for (const item of allSettingsQ.data ?? []) {
      if (item.socd_pair == null) continue;
      const a = Math.min(item.key_index, item.socd_pair);
      const b = Math.max(item.key_index, item.socd_pair);
      const key = `${a}-${b}`;
      if (seen.has(key)) continue;
      seen.add(key);
      pairs.push({ a, b, resolution: item.socd_resolution });
    }

    return pairs;
  }, [allSettingsQ.data]);

  const behaviorKeys = useMemo(() => {
    return (allSettingsQ.data ?? []).filter((item) => item.behavior_mode !== KEY_BEHAVIORS.Normal);
  }, [allSettingsQ.data]);

  const configuredByMenu = useMemo(() => {
    let tapHold = 0;
    let toggle = 0;
    let dynamic = 0;

    for (const item of behaviorKeys) {
      const menu = menuFromBehavior(item.behavior_mode);
      if (menu === "tap-hold") tapHold += 1;
      if (menu === "toggle") toggle += 1;
      if (menu === "dynamic") dynamic += 1;
    }

    return {
      socd: socdPairs.length,
      "tap-hold": tapHold,
      toggle,
      dynamic,
    } as const;
  }, [behaviorKeys, socdPairs.length]);

  const activeSocdPair = useMemo(() => {
    if (activeKeyIndex == null) return null;
    return settings?.socd_pair ?? settingsByIndex.get(activeKeyIndex)?.socd_pair ?? null;
  }, [activeKeyIndex, settings, settingsByIndex]);

  const canDeleteCurrent = useMemo(() => {
    if (activeKeyIndex == null) return false;
    if (activeMenu === "socd") return activeSocdPair != null;

    const currentBehavior = settings?.behavior_mode ?? settingsByIndex.get(activeKeyIndex)?.behavior_mode;
    return currentBehavior != null && currentBehavior !== KEY_BEHAVIORS.Normal;
  }, [activeKeyIndex, activeMenu, activeSocdPair, settings, settingsByIndex]);

  const handleDeleteCurrent = useCallback(() => {
    if (activeKeyIndex == null) return;

    if (activeMenu === "socd") {
      if (activeSocdPair == null) return;
      void clearSocdPair(activeKeyIndex, activeSocdPair);
      closeConfigurePanel();
      return;
    }

    void updateKeyByIndex(activeKeyIndex, { behavior_mode: KEY_BEHAVIORS.Normal });
    closeConfigurePanel();
  }, [activeKeyIndex, activeMenu, activeSocdPair, clearSocdPair, closeConfigurePanel, updateKeyByIndex]);

  useEffect(() => {
    const selected = new Set<string>();

    if (panelMode === "configure") {
      if (activeMenu === "socd") {
        if (assignState?.kind === "socd") {
          if (socdTouch1 != null) {
            selected.add(`key-${socdTouch1}`);
          }
          if (assignState.slot === 2 && socdTouch2 != null) {
            selected.add(`key-${socdTouch2}`);
          }
        } else {
          if (activeKeyIndex != null) {
            selected.add(`key-${activeKeyIndex}`);
          }
          if (activeSocdPair != null) {
            selected.add(`key-${activeSocdPair}`);
          }
        }
      } else if (activeKeyIndex != null) {
        selected.add(`key-${activeKeyIndex}`);
      }
    }

    setSelectedKeys(Array.from(selected));
  }, [
    activeKeyIndex,
    activeMenu,
    activeSocdPair,
    assignState,
    panelMode,
    setSelectedKeys,
    socdTouch1,
    socdTouch2,
  ]);

  useEffect(() => {
    return () => setSelectedKeys([]);
  }, [setSelectedKeys]);

  useEffect(() => {
    if (activeMenu !== "toggle") return;
    if (activeKeyIndex == null || !settings) return;
    if (settings.behavior_mode !== KEY_BEHAVIORS.Toggle) return;
    if (settings.secondary_hid_keycode === settings.hid_keycode) return;

    void updateKeyByIndex(activeKeyIndex, {
      secondary_hid_keycode: settings.hid_keycode,
    });
  }, [activeKeyIndex, activeMenu, settings, updateKeyByIndex]);

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

  function renderTapHoldConfig() {
    if (!settings) return null;
    const holdOnOtherKey = Boolean(settings.tap_hold_options & 0x01);
    const uppercaseHold = Boolean(settings.tap_hold_options & 0x02);
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
              <Badge variant="secondary">{keycodeDisplayName(settings.secondary_hid_keycode)}</Badge>
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
                min={50}
                max={1000}
                step={10}
                value={settings.hold_threshold_ms}
                onCommit={(v) => keyMutation.mutate({ hold_threshold_ms: v })}
                disabled={!connected}
              />
            </div>
            <div className="grid gap-2">
              <Label className="text-sm font-medium">Tick Rate</Label>
              <CommitSlider
                min={1}
                max={100}
                step={1}
                value={tickRateQ.data ?? 1}
                onCommit={(v) => kbheDevice.setAdvancedTickRate(v)}
                disabled={!connected}
              />
            </div>
            <FormRow label="Hold on Other Key Press" description="Trigger hold when another key is pressed">
              <Switch
                checked={holdOnOtherKey}
                disabled={!connected}
                onCheckedChange={(v) => {
                  const opts = (settings.tap_hold_options ?? 0);
                  keyMutation.mutate({ tap_hold_options: v ? opts | 0x01 : opts & ~0x01 });
                }}
              />
            </FormRow>
            <FormRow label="Uppercase Hold" description="Treat hold as Shift+key">
              <Switch
                checked={uppercaseHold}
                disabled={!connected}
                onCheckedChange={(v) => {
                  const opts = (settings.tap_hold_options ?? 0);
                  keyMutation.mutate({ tap_hold_options: v ? opts | 0x02 : opts & ~0x02 });
                }}
              />
            </FormRow>
            <FormRow label="Disable KB on Gamepad" description="Suppress keyboard output when gamepad is active">
              <Switch
                checked={settings.disable_kb_on_gamepad}
                disabled={!connected}
                onCheckedChange={(v) => keyMutation.mutate({ disable_kb_on_gamepad: v })}
              />
            </FormRow>
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
          <div className="flex flex-col gap-3">
            <p className="text-sm text-muted-foreground">
              Toggle uses the mapped key of the selected target. Remapping is disabled in this mode.
            </p>
          </div>
        </TabsContent>
        <TabsContent value="advanced" className="mt-4">
          <div className="grid gap-2">
            <Label className="text-sm font-medium">Tapping Term</Label>
            <CommitSlider
              min={50}
              max={1000}
              step={10}
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
    const zones = settings.dynamic_zones ?? [];
    const zone = zones[selectedDynamicZone] ?? { end_mm: 4.0, end_mm_tenths: 40, hid_keycode: 0 };
    const isLastZone = selectedDynamicZone === 3;

    const patchZone = (i: number, patch: Partial<typeof zone>) => {
      const next = zones.map((z, idx) => (idx === i ? { ...z, ...patch } : z));
      keyMutation.mutate({ dynamic_zones: next });
    };

    return (
      <Tabs defaultValue="zones" className="w-full">
        <TabsList className="w-full">
          <TabsTrigger value="zones">Zones</TabsTrigger>
          <TabsTrigger value="performance">Performance</TabsTrigger>
          <TabsTrigger value="tester">Key Tester</TabsTrigger>
        </TabsList>

        <TabsContent value="zones" className="mt-4">
          <div className="flex flex-col gap-4">
            <p className="text-xs text-amber-600 dark:text-amber-400 bg-amber-500/10 rounded-md px-3 py-2">
              Rapid Trigger is automatically disabled for Dynamic Mapping keys. Bottom Out Point controls the "fully pressed" threshold.
            </p>

            {/* Zone selector */}
            <div className="flex gap-1">
              {zones.slice(0, 4).map((z, i) => (
                <button
                  key={i}
                  type="button"
                  onClick={() => setSelectedDynamicZone(i)}
                  className={`flex-1 rounded-md border px-2 py-1.5 text-xs font-medium transition-colors ${
                    selectedDynamicZone === i
                      ? "border-primary bg-primary text-primary-foreground"
                      : z.hid_keycode !== 0
                      ? "border-green-500/50 bg-green-500/10 text-foreground hover:bg-green-500/20"
                      : "border-border bg-muted/30 text-muted-foreground hover:bg-muted"
                  }`}
                >
                  Zone {i + 1}
                  {z.hid_keycode !== 0 && (
                    <span className="ml-1 opacity-70 hidden sm:inline">
                      · {keycodeDisplayName(z.hid_keycode)}
                    </span>
                  )}
                </button>
              ))}
            </div>

            {/* Selected zone config */}
            <div className="flex flex-col gap-3 rounded-lg border p-3">
              <div className="flex items-center justify-between">
                <span className="text-sm font-medium">Zone {selectedDynamicZone + 1} Action</span>
                {zone.hid_keycode !== 0 && (
                  <Badge variant="secondary">{keycodeDisplayName(zone.hid_keycode)}</Badge>
                )}
              </div>

              {!isLastZone && (
                <DistanceSlider
                  label="Zone travel end"
                  value={zone.end_mm}
                  onChange={(v) => patchZone(selectedDynamicZone, { end_mm: v, end_mm_tenths: Math.round(v * 10) })}
                  disabled={!connected}
                />
              )}
              {isLastZone && (
                <p className="text-xs text-muted-foreground">Zone 4 covers travel from Zone 3 end to bottom-out.</p>
              )}

              <KeycodeAccordion
                selectedCode={zone.hid_keycode}
                onSelect={(code) => patchZone(selectedDynamicZone, { hid_keycode: code })}
                className="max-h-56"
              />
            </div>
          </div>
        </TabsContent>

        <TabsContent value="performance" className="mt-4">
          <div className="flex flex-col gap-4">
            <DistanceSlider
              label="Actuation Point"
              value={settings.actuation_point_mm}
              onChange={(v) => keyMutation.mutate({ actuation_point_mm: v })}
              disabled={!connected}
            />
            <DistanceSlider
              label="Bottom-Out Point"
              value={settings.dks_bottom_out_point_mm}
              onChange={(v) => keyMutation.mutate({ dks_bottom_out_point_mm: v })}
              disabled={!connected}
            />
            <FormRow label="Disable KB on Gamepad" description="Suppress keyboard output when gamepad is active">
              <Switch
                checked={settings.disable_kb_on_gamepad}
                disabled={!connected}
                onCheckedChange={(v) => keyMutation.mutate({ disable_kb_on_gamepad: v })}
              />
            </FormRow>
          </div>
        </TabsContent>

        <TabsContent value="tester" className="mt-4">
          <KeyTester />
        </TabsContent>
      </Tabs>
    );
  }

  function renderActiveMenu() {
    if (activeMenu === "socd") {
      const touch1Selecting = assignState?.kind === "socd" && assignState.slot === 1;
      const touch2Selecting = assignState?.kind === "socd" && assignState.slot === 2;
      const touch1Selected = socdTouch1 != null && !touch1Selecting;
      const touch2Selected = socdTouch2 != null && !touch2Selecting;

      return (
        <div className="flex flex-col gap-4">
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
            <div className={`rounded-lg border p-3 ${touch1Selecting ? "border-primary bg-primary/5" : touch1Selected ? "border-green-500/70 bg-green-500/5" : "border-border"}`}>
              <div className="flex items-start justify-between gap-3">
                <div className="min-w-0">
                  <p className="text-xs text-muted-foreground">Touch 1</p>
                  <p className="text-sm font-medium">{activeKeycodeName(socdTouch1)}</p>
                  <p className="mt-1 text-xs text-muted-foreground">
                    {touch1Selecting ? "Selecting..." : touch1Selected ? "Selected" : "Not configured"}
                  </p>
                </div>
                <button
                  type="button"
                  className="inline-flex size-8 items-center justify-center rounded-full bg-green-500 text-white transition hover:bg-green-600 disabled:cursor-not-allowed disabled:opacity-50"
                  onClick={() => {
                    setAssignState({ kind: "socd", slot: 1 });
                    setPendingSocd({ touch1: null, touch2: null });
                  }}
                  disabled={!connected}
                  aria-label="Pick SOCD touch 1"
                >
                  <IconPlus className="size-4" />
                </button>
              </div>
            </div>

            <div className={`rounded-lg border p-3 ${touch2Selecting ? "border-primary bg-primary/5" : touch2Selected ? "border-green-500/70 bg-green-500/5" : "border-border"}`}>
              <div className="flex items-start justify-between gap-3">
                <div className="min-w-0">
                  <p className="text-xs text-muted-foreground">Touch 2</p>
                  <p className="text-sm font-medium">{activeKeycodeName(socdTouch2)}</p>
                  <p className="mt-1 text-xs text-muted-foreground">
                    {touch2Selecting ? "Selecting..." : touch2Selected ? "Selected" : "Not configured"}
                  </p>
                </div>
                <button
                  type="button"
                  className="inline-flex size-8 items-center justify-center rounded-full bg-green-500 text-white transition hover:bg-green-600 disabled:cursor-not-allowed disabled:opacity-50"
                  onClick={() => {
                    if (socdTouch1 == null) return;
                    setPendingSocd({ touch1: socdTouch1, touch2: null });
                    setAssignState({ kind: "socd", slot: 2 });
                  }}
                  disabled={!connected || socdTouch1 == null}
                  aria-label="Pick SOCD touch 2"
                >
                  <IconPlus className="size-4" />
                </button>
              </div>
            </div>
          </div>

          {assignState?.kind === "socd" && (
            <FormRow
              label="Selection Mode"
              description={assignState.slot === 1
                ? "Click a key in the keyboard view to set Touch 1."
                : "Click another key in the keyboard view to set Touch 2."}
            >
              <Button variant="outline" size="sm" onClick={() => setAssignState(null)}>
                Cancel
              </Button>
            </FormRow>
          )}

          {assignState?.kind !== "socd" && (socdTouch1 == null || socdTouch2 == null) && (
            <p className="text-xs text-muted-foreground">
              {socdTouch1 == null
                ? "Touch 1 is not set. Click the + on Touch 1 and choose a key in the keyboard view."
                : "Touch 2 is not set. Click the + on Touch 2 and choose another key in the keyboard view."}
            </p>
          )}

          {activeKeyIndex != null && settings?.socd_pair != null && (
            <>
              <FormRow label="Resolution Mode" description="How conflicting inputs are resolved">
                <Select
                  value={String(settings.socd_resolution)}
                  disabled={!connected}
                  items={selectItems(SOCD_RESOLUTIONS)}
                  onValueChange={(v) => {
                    if (activeKeyIndex == null || settings.socd_pair == null) return;
                    void upsertSocdPair(activeKeyIndex, settings.socd_pair, Number(v));
                  }}
                >
                  <SelectTrigger className="h-8 w-56 text-sm"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    <SelectGroup>
                      {Object.entries(SOCD_RESOLUTIONS).map(([name, value]) => (
                        <SelectItem key={value} value={String(value)}>{name}</SelectItem>
                      ))}
                    </SelectGroup>
                  </SelectContent>
                </Select>
              </FormRow>
              <FormRow
                label="Alternative Fully Pressed Behavior"
                description="Register both key presses when keys are fully pressed simultaneously, bypassing the resolution behavior"
              >
                <Switch
                  checked={settings.socd_fully_pressed_enabled}
                  disabled={!connected}
                  onCheckedChange={(v: boolean) => keyMutation.mutate({ socd_fully_pressed_enabled: v })}
                />
              </FormRow>
              {settings.socd_fully_pressed_enabled && (
                <DistanceSlider
                  label="Fully Pressed Point"
                  value={settings.socd_fully_pressed_point_mm}
                  onChange={(v) => keyMutation.mutate({ socd_fully_pressed_point_mm: v })}
                  disabled={!connected}
                />
              )}
              <FormRow label="Disable KB on Gamepad" description="Suppress keyboard output when gamepad is active">
                <Switch
                  checked={settings.disable_kb_on_gamepad}
                  disabled={!connected}
                  onCheckedChange={(v: boolean) => keyMutation.mutate({ disable_kb_on_gamepad: v })}
                />
              </FormRow>
            </>
          )}
        </div>
      );
    }

    const behaviorMenu = activeMenu as Exclude<AdvancedMenu, "socd">;
    const selectingTarget = assignState?.kind === "behavior" && assignState.menu === behaviorMenu;
    const targetSelected = activeKeyIndex != null && !selectingTarget;

    return (
      <div className="flex flex-col gap-4">
        <div className={`rounded-lg border p-3 ${selectingTarget ? "border-primary bg-primary/5" : targetSelected ? "border-green-500/70 bg-green-500/5" : "border-border"}`}>
          <div className="flex items-start justify-between gap-3">
            <div className="min-w-0">
              <p className="text-xs text-muted-foreground">Target Key</p>
              <p className="text-sm font-medium">{activeKeycodeName(activeKeyIndex)}</p>
              <p className="mt-1 text-xs text-muted-foreground">
                {selectingTarget ? "Selecting..." : targetSelected ? "Selected" : "Not configured"}
              </p>
            </div>
            <button
              type="button"
              className="inline-flex size-8 items-center justify-center rounded-full bg-green-500 text-white transition hover:bg-green-600 disabled:cursor-not-allowed disabled:opacity-50"
              onClick={() => setAssignState({ kind: "behavior", menu: behaviorMenu })}
              disabled={!connected}
              aria-label="Pick target key"
            >
              <IconPlus className="size-4" />
            </button>
          </div>
        </div>

        {selectingTarget && (
          <FormRow label="Selection Mode" description="Click a key in the keyboard view to set the target key.">
            <Button variant="outline" size="sm" onClick={() => setAssignState(null)}>
              Cancel
            </Button>
          </FormRow>
        )}

        {activeKeyIndex == null && !selectingTarget && (
          <p className="text-sm text-muted-foreground">
            Target key is not set. Click the + button and choose a key in the keyboard view.
          </p>
        )}

        {activeKeyIndex != null && (
          <>
            {activeSettingsQ.isLoading && !settings ? (
              <div className="flex flex-col gap-3">{[0, 1, 2].map((i) => <Skeleton key={i} className="h-12 w-full" />)}</div>
            ) : !settings ? (
              <p className="text-sm text-muted-foreground">Could not load this advanced key.</p>
            ) : activeMenu === "tap-hold" ? (
              renderTapHoldConfig()
            ) : activeMenu === "toggle" ? (
              renderToggleConfig()
            ) : (
              renderDynamicConfig()
            )}
          </>
        )}
      </div>
    );
  }

  return (
    <KeyboardEditor
      keyboard={
        <BaseKeyboard
          mode="single"
          onButtonClick={handleKeyboardClick}
          onKeyHoverChange={handleKeyboardHoverChange}
          showLayerSelector={false}
          showRotary={false}
          keyLegendSlotsMap={keyboardLegendSlotsMap}
          loading={keyboardPreviewLoading}
          keyLegendClassName="text-[9px] leading-[1.05]"
          keyColorMap={keyboardKeyColorMap}
        />
      }
      menubar={menubar}
    >
      {panelMode === "configure" ? (
        <SectionCard
          title={ADVANCED_MENU_INFO.find((x) => x.id === activeMenu)?.label ?? activeMenu.toUpperCase()}
          description={ADVANCED_MENU_INFO.find((x) => x.id === activeMenu)?.description ?? "Configure this advanced option"}
        >
          <div className="flex flex-col gap-4">
            {renderActiveMenu()}
            <div className="flex items-center justify-end gap-2 border-t pt-4">
              <Button variant="destructive" disabled={!canDeleteCurrent} onClick={handleDeleteCurrent}>
                Delete
              </Button>
              <Button variant="outline" onClick={closeConfigurePanel}>
                Save
              </Button>
            </div>
          </div>
        </SectionCard>
      ) : (
        <div className="grid grid-cols-1 gap-4 xl:grid-cols-12">
          <div className="flex flex-col gap-4 xl:col-span-7">
            <SectionCard
              title="Advanced Options"
              description="Select an advanced option, then assign keys from the keyboard preview."
            >
              <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
                {ADVANCED_MENU_INFO.map((item) => (
                  <Button
                    key={item.id}
                    variant="outline"
                    className="h-auto flex-col items-start gap-2 p-4 text-left"
                    onClick={() => handleSelectMenu(item.id)}
                    disabled={!connected}
                  >
                    <div className="flex items-center gap-2">
                      <item.icon className="size-4" />
                      <span className="font-medium">{item.label}</span>
                    </div>
                    <span className="text-xs font-normal text-muted-foreground">{item.description}</span>
                    <div className="flex items-center gap-2">
                      <Badge variant={configuredByMenu[item.id] > 0 ? "default" : "secondary"}>
                        {configuredByMenu[item.id] > 0 ? `${configuredByMenu[item.id]} configured` : "Not configured"}
                      </Badge>
                    </div>
                  </Button>
                ))}
              </div>
            </SectionCard>
          </div>

          <div className="xl:col-span-5">
            <SectionCard title="Advanced Keys" description="Configured keys are displayed with their keycodes.">
              {allSettingsQ.isLoading ? (
                <div className="flex flex-col gap-3">{[0, 1, 2].map((i) => <Skeleton key={i} className="h-10 w-full" />)}</div>
              ) : (
                <div className="flex flex-col gap-3">
                  {socdPairs.map(({ a, b, resolution }) => {
                    const isHovered = hoveredAdvancedItem?.kind === "socd"
                      && hoveredAdvancedItem.a === a
                      && hoveredAdvancedItem.b === b;

                    return (
                      <Button
                        key={`socd-${a}-${b}`}
                        variant="outline"
                        className={cn(
                          "h-auto justify-start p-3 transition-colors",
                          isHovered && "border-green-500/70 bg-green-500/5 hover:bg-green-500/10",
                        )}
                        onMouseEnter={() => setHoveredAdvancedItem({ kind: "socd", a, b })}
                        onMouseLeave={() => setHoveredAdvancedItem(null)}
                        onFocus={() => setHoveredAdvancedItem({ kind: "socd", a, b })}
                        onBlur={() => setHoveredAdvancedItem(null)}
                        onClick={() => {
                          setPanelMode("configure");
                          setActiveMenu("socd");
                          setActiveKeyIndex(a);
                          setAssignState(null);
                          setPendingSocd({ touch1: null, touch2: null });
                          setHoveredAdvancedItem(null);
                        }}
                      >
                        <div className="flex w-full items-center justify-between gap-3">
                          <div className="flex flex-col items-start gap-1">
                            <span className="text-sm font-medium">SOCD</span>
                            <span className="text-xs text-muted-foreground">
                              {activeKeycodeName(a)} / {activeKeycodeName(b)}
                            </span>
                          </div>
                          <Badge variant="secondary">{selectItems(SOCD_RESOLUTIONS).find((x) => Number(x.value) === resolution)?.label ?? "SOCD"}</Badge>
                        </div>
                      </Button>
                    );
                  })}

                  {behaviorKeys.map((item) => {
                    const menu = menuFromBehavior(item.behavior_mode);
                    if (!menu) return null;
                    const isHovered = hoveredAdvancedItem?.kind === "behavior"
                      && hoveredAdvancedItem.keyIndex === item.key_index;

                    return (
                      <Button
                        key={`behavior-${item.key_index}`}
                        variant="outline"
                        className={cn(
                          "h-auto justify-start p-3 transition-colors",
                          isHovered && "border-green-500/70 bg-green-500/5 hover:bg-green-500/10",
                        )}
                        onMouseEnter={() => setHoveredAdvancedItem({ kind: "behavior", keyIndex: item.key_index })}
                        onMouseLeave={() => setHoveredAdvancedItem(null)}
                        onFocus={() => setHoveredAdvancedItem({ kind: "behavior", keyIndex: item.key_index })}
                        onBlur={() => setHoveredAdvancedItem(null)}
                        onClick={() => {
                          setPanelMode("configure");
                          setActiveMenu(menu);
                          setActiveKeyIndex(item.key_index);
                          setAssignState(null);
                          setHoveredAdvancedItem(null);
                        }}
                      >
                        <div className="flex w-full items-center justify-between gap-3">
                          <div className="flex flex-col items-start gap-1">
                            <span className="text-sm font-medium">{ADVANCED_MENU_INFO.find((x) => x.id === menu)?.label}</span>
                            <span className="text-xs text-muted-foreground">{activeKeycodeName(item.key_index)}</span>
                          </div>
                          <Badge variant="secondary">{activeKeycodeName(item.key_index)}</Badge>
                        </div>
                      </Button>
                    );
                  })}

                  {socdPairs.length === 0 && behaviorKeys.length === 0 && (
                    <p className="text-sm text-muted-foreground">
                      No advanced key assigned yet. Choose an option above and click keys in the keyboard preview.
                    </p>
                  )}
                </div>
              )}
            </SectionCard>
          </div>
        </div>
      )}
    </KeyboardEditor>
  );
}
