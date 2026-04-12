import { useCallback, useMemo } from "react";
import { useQuery } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useOSLayout } from "@/hooks/use-os-layout";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { KeycodeAccordion } from "@/components/keycode-accordion";
import { LayerSelect } from "@/components/layer-select";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { Button } from "@/components/ui/button";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { HID_KEYCODE_NAMES, KEY_COUNT } from "@/lib/kbhe/protocol";
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

export default function Keymap() {
  const selectedKeys    = useKeyboardStore((s) => s.selectedKeys);
  const currentLayer    = useKeyboardStore((s) => s.currentLayer);
  const setCurrentLayer = useKeyboardStore((s) => s.setCurrentLayer);
  const updateKeyConfig = useKeyboardStore((s) => s.updateKeyConfig);
  const clearSelection  = useKeyboardStore((s) => s.clearSelection);
  const { status }      = useDeviceSession();
  const connected       = status === "connected";
  const { saveState, markSaving, markSaved } = useAutosave();
  const resolveKeycodeLabel = useOSLayout();

  const layerKeycodes = useQuery({
    queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer),
    queryFn: () => fetchAllLayerKeycodes(currentLayer),
    enabled: connected,
    staleTime: 30_000,
  });

  const setKeycodeMutation = useOptimisticMutation({
    queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer),
    mutationFn: async ({ keyIndex, keycode }: { keyIndex: number; keycode: number }) => {
      markSaving();
      await kbheDevice.setLayerKeycode(currentLayer, keyIndex, keycode);
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

  const keyLegendMap = useMemo(() => {
    if (!layerKeycodes.data) return undefined;

    const map: Record<string, string> = {};
    for (const [index, code] of Object.entries(layerKeycodes.data)) {
      const numericCode = Number(code);
      const fallback = HID_KEYCODE_NAMES[numericCode] ?? `0x${numericCode.toString(16)}`;
      map[`key-${index}`] = resolveKeycodeLabel(numericCode, fallback);
    }
    return map;
  }, [layerKeycodes.data, resolveKeycodeLabel]);

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        <LayerSelect value={currentLayer} onChange={setCurrentLayer} />
      </div>
      <div className="flex items-center gap-2">
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
          onButtonClick={() => {}}
          showLayerSelector={false}
          showRotary={false}
          keyLegendMap={connected ? keyLegendMap : undefined}
          keyLegendClassName="text-[9px] leading-tight truncate"
        />
      }
      menubar={menubar}
    >
      <KeycodeAccordion
        onSelect={handleKeycodeSelect}
        selectedCode={selectedCode}
        className="h-full"
      />
    </KeyboardEditor>
  );
}
