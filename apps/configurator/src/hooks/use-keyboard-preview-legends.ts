import { createElement, useMemo, type ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { useOSKeycapLegend } from "@/hooks/use-os-layout";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type KeyGamepadMap } from "@/lib/kbhe/device";
import {
    GAMEPAD_KEYBOARD_ROUTING,
    GAMEPAD_LAYER_MASK_ALL,
    HID_KEYCODE_NAMES,
    HID_KEYCODES,
    KEY_COUNT,
} from "@/lib/kbhe/protocol";
import { buildKeycodeLegendSlots } from "@/lib/kbhe/keycode-icons";
import { LEGEND_POSITION_CLASSES } from "@/lib/vendor/react-kle-modern/utils";
import { previewKeys } from "@/constants/defaultLayout";
import { queryKeys } from "@/lib/query/keys";

const EMPTY_KEY_SLOTS: Array<ReactNode | undefined> = Array.from({ length: 12 }, () => "");
const BATCH_SIZE = 8;

const GAMEPAD_AXIS_ABBREVIATION: Record<number, string> = {
    1: "LX",
    2: "LY",
    3: "RX",
    4: "RY",
    5: "LT",
    6: "RT",
};

const GAMEPAD_BUTTON_ABBREVIATION: Record<number, string> = {
    1: "A",
    2: "B",
    3: "X",
    4: "Y",
    5: "LB",
    6: "RB",
    7: "LT",
    8: "RT",
    9: "BK",
    10: "ST",
    11: "L3",
    12: "R3",
    13: "DU",
    14: "DD",
    15: "DL",
    16: "DR",
    17: "HM",
};

async function fetchAllLayerKeycodes(layer: number): Promise<Record<number, number>> {
    const codes: Record<number, number> = {};

    for (let start = 0; start < KEY_COUNT; start += BATCH_SIZE) {
        const end = Math.min(start + BATCH_SIZE, KEY_COUNT);
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

async function fetchAllLayerGamepadMaps(layer: number): Promise<Record<number, KeyGamepadMap>> {
    const maps: Record<number, KeyGamepadMap> = {};

    for (let start = 0; start < KEY_COUNT; start += BATCH_SIZE) {
        const end = Math.min(start + BATCH_SIZE, KEY_COUNT);
        const batch = Array.from({ length: end - start }, (_, i) =>
            kbheDevice.getKeyGamepadMap(start + i),
        );
        const results = await Promise.all(batch);

        for (let i = 0; i < results.length; i += 1) {
            const result = results[i];
            if (!result) {
                continue;
            }

            const keyIndex = start + i;
            const layerBit = 1 << layer;
            const layerMask = result.layer_mask || GAMEPAD_LAYER_MASK_ALL;
            if ((layerMask & layerBit) === 0) {
                continue;
            }

            maps[keyIndex] = result;
        }
    }

    return maps;
}

function isGamepadActionKeycode(code: number): boolean {
    return (
        (code >= 0xf300 && code <= 0xf302) ||
        (code >= 0xf320 && code <= 0xf330) ||
        (code >= 0xf340 && code <= 0xf347)
    );
}

function hasGamepadMapping(mapping: KeyGamepadMap | undefined): boolean {
    if (!mapping) {
        return false;
    }

    return mapping.axis !== 0 || mapping.button !== 0;
}

function shouldShowKeyboardLegend(
    code: number,
    keyHasGamepadMapping: boolean,
    keyboardEnabled: boolean,
    gamepadEnabled: boolean,
    keyboardRouting: number,
): boolean {
    if (code === HID_KEYCODES.TRANSPARENT) {
        return false;
    }

    if (isGamepadActionKeycode(code)) {
        return true;
    }

    if (!keyboardEnabled) {
        return false;
    }

    if (!gamepadEnabled) {
        return true;
    }

    if (keyboardRouting === GAMEPAD_KEYBOARD_ROUTING.Disabled) {
        return false;
    }

    if (keyboardRouting === GAMEPAD_KEYBOARD_ROUTING["Unmapped Only"]) {
        return !keyHasGamepadMapping;
    }

    return true;
}

function mergeGamepadLegend(
    baseSlots: Array<ReactNode | undefined>,
    mapping: KeyGamepadMap | undefined,
): Array<ReactNode | undefined> {
    if (!hasGamepadMapping(mapping)) {
        return baseSlots;
    }

    const slots = [...baseSlots];

    if ((mapping?.button ?? 0) !== 0) {
        const shortName = GAMEPAD_BUTTON_ABBREVIATION[mapping?.button ?? 0] ?? `B${mapping?.button ?? 0}`;
        slots[0] = `GP ${shortName}`;
    }

    if ((mapping?.axis ?? 0) !== 0) {
        const axis = GAMEPAD_AXIS_ABBREVIATION[mapping?.axis ?? 0] ?? `A${mapping?.axis ?? 0}`;
        const sign = mapping?.direction === 1 ? "-" : "+";
        slots[8] = `${axis}${sign}`;
    }

    return slots;
}

function buildGamepadCompactLabel(mapping: KeyGamepadMap | undefined): string {
    if (!hasGamepadMapping(mapping)) {
        return "";
    }

    const parts: string[] = [];

    if ((mapping?.button ?? 0) !== 0) {
        const shortName = GAMEPAD_BUTTON_ABBREVIATION[mapping?.button ?? 0] ?? `B${mapping?.button ?? 0}`;
        parts.push(shortName);
    }

    if ((mapping?.axis ?? 0) !== 0) {
        const axis = GAMEPAD_AXIS_ABBREVIATION[mapping?.axis ?? 0] ?? `A${mapping?.axis ?? 0}`;
        const sign = mapping?.direction === 1 ? "-" : "+";
        parts.push(`${axis}${sign}`);
    }

    return parts.join("/");
}

function buildGamepadSplitLabel(mapping: KeyGamepadMap | undefined): string {
    if (!hasGamepadMapping(mapping)) {
        return "";
    }

    const parts: string[] = [];

    if ((mapping?.button ?? 0) !== 0) {
        const shortName = GAMEPAD_BUTTON_ABBREVIATION[mapping?.button ?? 0] ?? `B${mapping?.button ?? 0}`;
        parts.push(shortName);
    }

    if ((mapping?.axis ?? 0) !== 0) {
        const axis = GAMEPAD_AXIS_ABBREVIATION[mapping?.axis ?? 0] ?? `A${mapping?.axis ?? 0}`;
        const sign = mapping?.direction === 1 ? "-" : "+";
        parts.push(`${axis}${sign}`);
    }

    return parts.join("\n");
}

function hasVisibleSlotContent(slot: ReactNode | undefined): boolean {
    if (slot === null || slot === undefined) {
        return false;
    }

    if (typeof slot === "string") {
        return slot.trim().length > 0;
    }

    return true;
}

function buildSplitOverlayLegend(
    keyboardSlots: Array<ReactNode | undefined>,
    keyboardLabel: string,
    gamepadLabel: string,
): ReactNode {
    const tooltipText = `${gamepadLabel.replace(/\n+/g, " ").trim()} / ${keyboardLabel.replace(/\n+/g, " ").trim()}`.trim();

    const keyboardSlotNodes: ReactNode[] = [];
    keyboardSlots.forEach((slot, index) => {
        if (!hasVisibleSlotContent(slot)) {
            return;
        }

        keyboardSlotNodes.push(
            createElement(
                "span",
                {
                    key: `kb-${index}`,
                    className: `kle-legend ${LEGEND_POSITION_CLASSES[index]} kbhe-gp-split-keyboard-slot kbhe-gp-split-keyboard-slot--special`,
                },
                slot,
            ),
        );
    });

    const hasSpecialKeyboardLayout = keyboardSlotNodes.length > 1;

    const keyboardContent = hasSpecialKeyboardLayout
        ? createElement(
            "span",
            {
                key: "keyboard-layout",
                className: "kbhe-gp-split-keyboard-layout kbhe-gp-split-keyboard-layout--special",
                title: keyboardLabel,
            },
            keyboardSlotNodes,
        )
        : createElement(
            "span",
            {
                key: "keyboard-simple",
                className: "kbhe-gp-split-keyboard-simple kbhe-gp-split-keyboard-simple--simple",
                title: keyboardLabel,
            },
            keyboardLabel,
        );

    return createElement(
        "span",
        {
            className: `kbhe-gp-split-overlay ${hasSpecialKeyboardLayout
                ? "kbhe-gp-split-overlay--special"
                : "kbhe-gp-split-overlay--simple"}`,
            "data-keycap-text": tooltipText,
            title: tooltipText,
        },
        [
            createElement(
                "svg",
                {
                    key: "divider",
                    className: "kbhe-gp-split-divider",
                    viewBox: "0 0 100 100",
                    preserveAspectRatio: "none",
                    "aria-hidden": true,
                },
                [
                    createElement("polygon", {
                        key: "tri-gamepad",
                        className: "kbhe-gp-split-triangle kbhe-gp-split-triangle--gamepad",
                        points: "0,0 100,0 0,100",
                    }),
                    createElement("polygon", {
                        key: "tri-keyboard",
                        className: "kbhe-gp-split-triangle kbhe-gp-split-triangle--keyboard",
                        points: "100,0 100,100 0,100",
                    }),
                    createElement("line", {
                        key: "divider-line",
                        x1: 0,
                        y1: 100,
                        x2: 100,
                        y2: 0,
                    }),
                ],
            ),
            createElement(
                "span",
                {
                    key: "gamepad",
                    className: `kbhe-gp-split-gamepad ${hasSpecialKeyboardLayout
                        ? "kbhe-gp-split-gamepad--special"
                        : "kbhe-gp-split-gamepad--simple"}`,
                    title: gamepadLabel,
                },
                gamepadLabel,
            ),
            keyboardContent,
        ],
    );
}

export function useKeyboardPreviewLegends() {
    const currentLayer = useKeyboardStore((s) => s.currentLayer);
    const { status, activeProfileIndex } = useDeviceSession();
    const connected = status === "connected";
    const profileContext = activeProfileIndex ?? -1;
    const resolveKeycapLegend = useOSKeycapLegend();

    const layerKeycodes = useQuery({
        queryKey: queryKeys.keymap.allLayerKeycodes(currentLayer, profileContext),
        queryFn: () => fetchAllLayerKeycodes(currentLayer),
        enabled: connected,
        staleTime: 30_000,
    });

    const gamepadSettings = useQuery({
        queryKey: queryKeys.gamepad.settings(),
        queryFn: () => kbheDevice.getGamepadSettings(),
        enabled: connected,
        staleTime: 30_000,
    });

    const options = useQuery({
        queryKey: queryKeys.device.options(),
        queryFn: () => kbheDevice.getOptions(),
        enabled: connected,
        staleTime: 30_000,
    });

    const allLayerGamepadMaps = useQuery({
        queryKey: queryKeys.gamepad.allKeyMaps(currentLayer),
        queryFn: () => fetchAllLayerGamepadMaps(currentLayer),
        enabled: connected,
        staleTime: 30_000,
    });

    const { keyLegendSlotsMap, keyLegendOverlayMap } = useMemo(() => {
        const slotMap: Record<string, Array<ReactNode | undefined>> = {};
        const overlayMap: Record<string, ReactNode | undefined> = {};

        for (const key of previewKeys) {
            slotMap[key.id] = [...EMPTY_KEY_SLOTS];
            overlayMap[key.id] = undefined;
        }

        if (!connected || !layerKeycodes.data) {
            return {
                keyLegendSlotsMap: slotMap,
                keyLegendOverlayMap: overlayMap,
            };
        }

        const keyboardRouting =
            gamepadSettings.data?.keyboard_routing ?? GAMEPAD_KEYBOARD_ROUTING["All Keys"];
        const keyboardEnabled = options.data?.keyboard_enabled ?? false;
        const gamepadEnabled = options.data?.gamepad_enabled ?? false;

        for (const key of previewKeys) {
            const keyIndex = Number(key.id.replace("key-", ""));
            const numericCode = Number(layerKeycodes.data[keyIndex] ?? HID_KEYCODES.TRANSPARENT);
            const mapping = allLayerGamepadMaps.data?.[keyIndex];
            const keyHasGamepadMapping = hasGamepadMapping(mapping);
            const showKeyboard = shouldShowKeyboardLegend(
                numericCode,
                keyHasGamepadMapping,
                keyboardEnabled,
                gamepadEnabled,
                keyboardRouting,
            );

            let slots: Array<ReactNode | undefined> = [...EMPTY_KEY_SLOTS];
            let keyboardLegendText = "";
            if (showKeyboard) {
                const fallback = HID_KEYCODE_NAMES[numericCode] ?? `0x${numericCode.toString(16)}`;
                const legend = resolveKeycapLegend(numericCode, fallback);
                keyboardLegendText = legend.text;
                slots = buildKeycodeLegendSlots(numericCode, legend.slots, "size-3.5");
            }

            const gamepadLabel = buildGamepadCompactLabel(mapping);
            if (gamepadLabel.length > 0) {
                const shouldSplitKeyboardAndGamepad =
                    showKeyboard &&
                    gamepadEnabled &&
                    keyboardRouting === GAMEPAD_KEYBOARD_ROUTING["All Keys"] &&
                    !isGamepadActionKeycode(numericCode);

                if (shouldSplitKeyboardAndGamepad) {
                    const splitGamepadLabel = buildGamepadSplitLabel(mapping);
                    const splitKeyboardLabel = keyboardLegendText || (HID_KEYCODE_NAMES[numericCode] ?? "");
                    const splitKeyboardSlots = [...slots];

                    if (splitGamepadLabel.length > 0 && splitKeyboardLabel.length > 0) {
                        overlayMap[key.id] = buildSplitOverlayLegend(
                            splitKeyboardSlots,
                            splitKeyboardLabel,
                            splitGamepadLabel,
                        );
                        slots = [...EMPTY_KEY_SLOTS];
                    } else {
                        slots = mergeGamepadLegend(slots, mapping);
                    }
                } else {
                    slots = mergeGamepadLegend(slots, mapping);
                }
            }

            slotMap[key.id] = slots;
        }

        return {
            keyLegendSlotsMap: slotMap,
            keyLegendOverlayMap: overlayMap,
        };
    }, [
        allLayerGamepadMaps.data,
        connected,
        gamepadSettings.data?.keyboard_routing,
        layerKeycodes.data,
        options.data?.gamepad_enabled,
        options.data?.keyboard_enabled,
        resolveKeycapLegend,
    ]);

    const isLoading =
        connected && (
            (layerKeycodes.isLoading && !layerKeycodes.data) ||
            (allLayerGamepadMaps.isLoading && !allLayerGamepadMaps.data) ||
            !resolveKeycapLegend.isReady
        );

    return {
        keyLegendSlotsMap,
        keyLegendOverlayMap,
        isLoading,
    };
}
