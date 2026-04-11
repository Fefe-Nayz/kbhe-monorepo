import { useEffect, useCallback } from "react";
import { useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import KeyMapper from "@/components/keyMapper";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { HID_KEYCODES } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { PageHeader } from "@/components/shared/PageLayout";
import { cn } from "@/lib/utils";

export default function Keymap() {
  const selectedKeys      = useKeyboardStore((s) => s.selectedKeys);
  const currentLayer      = useKeyboardStore((s) => s.currentLayer);
  const updateKeyConfig   = useKeyboardStore((s) => s.updateKeyConfig);
  const setSaveEnabled    = useKeyboardStore((s) => s.setSaveEnabled);
  const { status }        = useDeviceSession();
  const connected         = status === "connected";
  const qc                = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  useEffect(() => {
    setSaveEnabled(true);
    return () => setSaveEnabled(false);
  }, [setSaveEnabled]);

  // Mutation: write a keycode to the device for the current layer
  const setKeycodeMutation = useMutation({
    mutationFn: async ({ keyIndex, keycode }: { keyIndex: number; keycode: number }) => {
      await kbheDevice.setLayerKeycode(currentLayer, keyIndex, keycode);
    },
    onMutate: markSaving,
    onSuccess: () => {
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer) });
    },
    onError: markError,
  });

  const handleKeyAssign = useCallback(
    (key: { id: string; label: string; value: string; width: number }) => {
      if (selectedKeys.length === 0) return;

      // Update local profile state
      updateKeyConfig(selectedKeys, { label: [key.label], value: key.value });

      // Write to device if connected
      if (connected) {
        for (const keyId of selectedKeys) {
          if (!keyId.startsWith("key-")) continue;
          const keyIndex = parseInt(keyId.replace("key-", ""), 10);
          if (!Number.isFinite(keyIndex)) continue;
          const keycode = HID_KEYCODES[key.value] ?? HID_KEYCODES[key.label] ?? 0;
          setKeycodeMutation.mutate({ keyIndex, keycode });
        }
      }

      useKeyboardStore.getState().clearSelection();
    },
    [selectedKeys, updateKeyConfig, connected, currentLayer, setKeycodeMutation],
  );

  return (
    <div className="flex flex-col h-full overflow-hidden">
      {/* Header bar */}
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader
          title="Keymap"
          description={`Layer ${currentLayer} — click a key, then pick an action`}
        />
        <AutosaveStatus state={saveState} />
      </div>

      {/* Body: keyboard top, mapper below */}
      <div className="flex flex-col flex-1 min-h-0 overflow-hidden">
        {/* Keyboard preview — fixed height */}
        <div className="shrink-0 border-b px-4 py-4 overflow-x-auto bg-muted/20">
          <BaseKeyboard
            mode="multi"
            onButtonClick={() => {}}
          />
        </div>

        {/* Key mapper — scrollable */}
        <div className="flex-1 overflow-y-auto p-4">
          {selectedKeys.length > 0 ? (
            <div className="flex flex-col gap-2 max-w-3xl mx-auto">
              <div className="text-xs text-muted-foreground">
                {selectedKeys.length} key{selectedKeys.length > 1 ? "s" : ""} selected
                &mdash; pick an action below to assign
              </div>
              <KeyMapper onButtonClick={handleKeyAssign} />
            </div>
          ) : (
            <div className={cn(
              "flex flex-col items-center justify-center h-32",
              "text-muted-foreground text-sm",
            )}>
              Select one or more keys on the keyboard above, then choose an action here.
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
