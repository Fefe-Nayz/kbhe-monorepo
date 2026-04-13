import { useMemo, type ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { useOSKeycapLegend } from "@/hooks/use-os-layout";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { HID_KEYCODE_NAMES, HID_KEYCODES, KEY_COUNT } from "@/lib/kbhe/protocol";
import { buildKeycodeLegendSlots } from "@/lib/kbhe/keycode-icons";
import { previewKeys } from "@/constants/defaultLayout";
import { queryKeys } from "@/lib/query/keys";

const EMPTY_KEY_SLOTS: Array<string | undefined> = Array.from({ length: 12 }, () => "");

async function fetchAllLayerKeycodes(layer: number): Promise<Record<number, number>> {
    const codes: Record<number, number> = {};
    const batchSize = 8;

    for (let start = 0; start < KEY_COUNT; start += batchSize) {
        const end = Math.min(start + batchSize, KEY_COUNT);
        const batch = Array.from({ length: end - start }, (_, i) =>
            kbheDevice.getLayerKeycode(layer, start + i),
        );
        const results = await Promise.all(batch);

        for (let i = 0; i < results.length; i += 1) {
            const result = results[i];
            if (result) {
                codes[start + i] = result.hid_keycode;
            }
        }
    }

    return codes;
}

export function useKeyboardPreviewLegends() {
    const currentLayer = useKeyboardStore((s) => s.currentLayer);
    const { status } = useDeviceSession();
    const connected = status === "connected";
    const resolveKeycapLegend = useOSKeycapLegend();

    const layerKeycodes = useQuery({
        queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer),
        queryFn: () => fetchAllLayerKeycodes(currentLayer),
        enabled: connected,
        staleTime: 30_000,
    });

    const keyLegendSlotsMap = useMemo(() => {
        const map: Record<string, Array<ReactNode | undefined>> = {};

        for (const key of previewKeys) {
            map[key.id] = [...EMPTY_KEY_SLOTS];
        }

        if (!connected || !layerKeycodes.data) {
            return map;
        }

        for (const [index, code] of Object.entries(layerKeycodes.data)) {
            const numericCode = Number(code);

            if (numericCode === HID_KEYCODES.TRANSPARENT) {
                map[`key-${index}`] = [...EMPTY_KEY_SLOTS];
                continue;
            }

            const fallback = HID_KEYCODE_NAMES[numericCode] ?? `0x${numericCode.toString(16)}`;
            const legend = resolveKeycapLegend(numericCode, fallback);
            map[`key-${index}`] = buildKeycodeLegendSlots(numericCode, legend.slots, "size-3.5");
        }

        return map;
    }, [connected, layerKeycodes.data, resolveKeycapLegend]);

    const isLoading =
        connected && (
            (layerKeycodes.isLoading && !layerKeycodes.data) ||
            !resolveKeycapLegend.isReady
        );

    return {
        keyLegendSlotsMap,
        isLoading,
    };
}
