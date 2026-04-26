import { useCallback } from "react";
import { useQuery } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useOSKeycapLegend } from "@/hooks/use-os-layout";
import { useKeyboardPreviewLegends } from "@/hooks/use-keyboard-preview-legends";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { KeycodeAccordion } from "@/components/keycode-accordion";
import { LayerSelect } from "@/components/layer-select";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { Button } from "@/components/ui/button";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { patchActiveAppProfileSnapshot } from "@/lib/kbhe/profile-snapshot-store";
import {
  GAMEPAD_AXIS_NAMES,
  GAMEPAD_BUTTON_NAMES,
  GAMEPAD_DIRECTIONS,
  GAMEPAD_LAYER_MASK_ALL,
  HID_KEYCODES,
  KEY_COUNT,
} from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { IconRestore } from "@tabler/icons-react";

async function fetchAllLayerKeycodes(layer: number): Promise<Record<number, number>> {
  const codes: Record<number, number> = {};
  const BATCH = 8;
  for (let start = 0; start < KEY_COUNT; start += BATCH) {
    const end = Math.min(start + BATCH, KEY_COUNT);
    const batch = Array.from({ length: end - start }, (_, i) =>
      kbheDevice.getLayerKeycode(layer, start + i),
    );
    const results = await Promise.all(batch);
    for (let i = 0; i < results.length; i++) {
      const r = results[i];
      if (r) codes[start + i] = r.hid_keycode;
    }
  }
  return codes;
}

const GAMEPAD_BUTTON_TO_KEYCODE: Record<number, number | undefined> = {
  1: HID_KEYCODES["GP A"],
  2: HID_KEYCODES["GP B"],
  3: HID_KEYCODES["GP X"],
  4: HID_KEYCODES["GP Y"],
  5: HID_KEYCODES["GP LB"],
  6: HID_KEYCODES["GP RB"],
  7: HID_KEYCODES["GP LT Trigger"],
  8: HID_KEYCODES["GP RT Trigger"],
  9: HID_KEYCODES["GP Back"],
  10: HID_KEYCODES["GP Start"],
  11: HID_KEYCODES["GP L3"],
  12: HID_KEYCODES["GP R3"],
  13: HID_KEYCODES["GP DPad Up"],
  14: HID_KEYCODES["GP DPad Down"],
  15: HID_KEYCODES["GP DPad Left"],
  16: HID_KEYCODES["GP DPad Right"],
  17: HID_KEYCODES["GP Home"],
};

function mapAxisToGamepadKeycode(axis: number, direction: number): number | undefined {
  const positive = direction !== GAMEPAD_DIRECTIONS["-"];

  if (axis === 1) {
    return positive ? HID_KEYCODES["GP LS Right"] : HID_KEYCODES["GP LS Left"];
  }
  if (axis === 2) {
    return positive ? HID_KEYCODES["GP LS Down"] : HID_KEYCODES["GP LS Up"];
  }
  if (axis === 3) {
    return positive ? HID_KEYCODES["GP RS Right"] : HID_KEYCODES["GP RS Left"];
  }
  if (axis === 4) {
    return positive ? HID_KEYCODES["GP RS Down"] : HID_KEYCODES["GP RS Up"];
  }
  if (axis === 5) {
    return HID_KEYCODES["GP LT Trigger"];
  }
  if (axis === 6) {
    return HID_KEYCODES["GP RT Trigger"];
  }

  return undefined;
}

export default function Keymap() {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const currentLayer = useKeyboardStore((s) => s.currentLayer);
  const setCurrentLayer = useKeyboardStore((s) => s.setCurrentLayer);
  const updateKeyConfig = useKeyboardStore((s) => s.updateKeyConfig);
  const clearSelection = useKeyboardStore((s) => s.clearSelection);
  const { status, activeProfileIndex } = useDeviceSession();
  const connected = status === "connected";
  const profileContext = activeProfileIndex ?? -1;
  const { saveState, markSaving, markSaved } = useAutosave();
  const resolveKeycapLegend = useOSKeycapLegend();
  const {
    keyLegendSlotsMap,
    keyLegendOverlayMap,
    isLoading: keyboardPreviewLoading,
  } = useKeyboardPreviewLegends();

  const focusedKeyId = selectedKeys[0] ?? null;
  const focusedKeyIndex = focusedKeyId?.startsWith("key-")
    ? Number.parseInt(focusedKeyId.replace("key-", ""), 10)
    : null;

  const focusedGamepadMap = useQuery({
    queryKey: queryKeys.gamepad.keyMap(focusedKeyIndex ?? -1, currentLayer),
    queryFn: () => (focusedKeyIndex != null ? kbheDevice.getKeyGamepadMap(focusedKeyIndex) : null),
    enabled: connected && focusedKeyIndex != null,
    staleTime: 15_000,
  });

  const layerKeycodes = useQuery({
    queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer, profileContext),
    queryFn: () => fetchAllLayerKeycodes(currentLayer),
    enabled: connected,
    staleTime: 30_000,
  });

  const setKeycodeMutation = useOptimisticMutation({
    queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer, profileContext),
    mutationFn: async ({ keyIndex, keycode }: { keyIndex: number; keycode: number }) => {
      markSaving();
      const ok = await kbheDevice.setLayerKeycode(currentLayer, keyIndex, keycode);
      if (!ok) {
        throw new Error(`Unable to update keycode for key ${keyIndex}`);
      }
      patchActiveAppProfileSnapshot((snapshot) => {
        const entry = snapshot.keySettings.find(
          (item) => item.key_index === keyIndex && item.layer_index === currentLayer,
        );
        if (entry) {
          entry.hid_keycode = keycode;
          entry.profile_index = profileContext;
        }
      });
    },
    optimisticUpdate: (cur, { keyIndex, keycode }) => ({ ...(cur ?? {}), [keyIndex]: keycode }),
    onSuccess: () => markSaved(),
  });

  const handleKeycodeSelect = useCallback(
    (code: number, name: string) => {
      if (selectedKeys.length === 0) return;

      updateKeyConfig(selectedKeys, { label: [name], value: name });

      if (connected) {
        for (const keyId of selectedKeys) {
          if (!keyId.startsWith("key-")) continue;
          const keyIndex = parseInt(keyId.replace("key-", ""), 10);
          if (!Number.isFinite(keyIndex)) continue;
          setKeycodeMutation.mutate({ keyIndex, keycode: code });
        }
      }

      clearSelection();
    },
    [selectedKeys, updateKeyConfig, connected, setKeycodeMutation, clearSelection],
  );

  const selectedCode = (() => {
    if (selectedKeys.length !== 1) return undefined;
    const keyId = selectedKeys[0];
    if (!keyId.startsWith("key-")) return undefined;
    const idx = parseInt(keyId.replace("key-", ""), 10);
    return layerKeycodes.data?.[idx];
  })();

  const focusedGamepadSummary = (() => {
    if (focusedKeyIndex == null || !focusedGamepadMap.data) {
      return null;
    }

    const mapping = focusedGamepadMap.data;
    const currentLayerBit = 1 << currentLayer;
    const activeLayerMask = mapping.layer_mask || GAMEPAD_LAYER_MASK_ALL;
    const mappingActiveOnLayer = (activeLayerMask & currentLayerBit) !== 0;
    if (!mappingActiveOnLayer || (mapping.axis === 0 && mapping.button === 0)) {
      return null;
    }

    const parts: string[] = [];
    if (mapping.button !== 0) {
      parts.push(GAMEPAD_BUTTON_NAMES[mapping.button] ?? `Button ${mapping.button}`);
    }
    if (mapping.axis !== 0) {
      const axis = GAMEPAD_AXIS_NAMES[mapping.axis] ?? `Axis ${mapping.axis}`;
      const direction = mapping.direction === GAMEPAD_DIRECTIONS["-"] ? "-" : "+";
      parts.push(`${axis} ${direction}`);
    }

    const summary = parts.join(" + ");
    return summary.length > 0 ? summary : null;
  })();

  const focusedGamepadSelectedCodes = (() => {
    if (focusedKeyIndex == null || !focusedGamepadMap.data) {
      return [];
    }

    const mapping = focusedGamepadMap.data;
    const layerMask = mapping.layer_mask || GAMEPAD_LAYER_MASK_ALL;
    if ((layerMask & (1 << currentLayer)) === 0) {
      return [];
    }

    const resolved = new Set<number>();
    const buttonCode = GAMEPAD_BUTTON_TO_KEYCODE[mapping.button];
    if (typeof buttonCode === "number") {
      resolved.add(buttonCode);
    }

    const axisCode = mapAxisToGamepadKeycode(mapping.axis, mapping.direction);
    if (typeof axisCode === "number") {
      resolved.add(axisCode);
    }

    return Array.from(resolved);
  })();

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        <LayerSelect value={currentLayer} onChange={setCurrentLayer} />
      </div>
      <div className="flex items-center gap-2">
        {focusedGamepadSummary && (
          <span className="rounded-md border px-2 py-1 text-xs text-muted-foreground">
            GP: {focusedGamepadSummary}
          </span>
        )}
        <AutosaveStatus state={saveState} />
        <Button variant="destructive" size="sm" className="h-8 gap-1.5" disabled={!connected}>
          <IconRestore className="size-4" />
          Reset Layer
        </Button>
      </div>
    </>
  );

  return (
    <KeyboardEditor
      keyboard={
        <BaseKeyboard
          mode="single"
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
      <KeycodeAccordion
        onSelect={handleKeycodeSelect}
        selectedCode={selectedCode}
        selectedCodes={focusedGamepadSelectedCodes}
        className="h-full"
        resolveLegend={resolveKeycapLegend}
      />
    </KeyboardEditor>
  );
}
