import { useCallback, useEffect, useMemo, useState } from "react";
import { invoke } from "@tauri-apps/api/core";

/**
 * Maps DOM KeyboardEvent.code → HID keycode (subset for printable keys).
 * Reversed: HID keycode → DOM code, so we can look up the OS label.
 */
export const HID_TO_DOM_CODE: Record<number, string> = {
  0x04: "KeyA", 0x05: "KeyB", 0x06: "KeyC", 0x07: "KeyD",
  0x08: "KeyE", 0x09: "KeyF", 0x0a: "KeyG", 0x0b: "KeyH",
  0x0c: "KeyI", 0x0d: "KeyJ", 0x0e: "KeyK", 0x0f: "KeyL",
  0x10: "KeyM", 0x11: "KeyN", 0x12: "KeyO", 0x13: "KeyP",
  0x14: "KeyQ", 0x15: "KeyR", 0x16: "KeyS", 0x17: "KeyT",
  0x18: "KeyU", 0x19: "KeyV", 0x1a: "KeyW", 0x1b: "KeyX",
  0x1c: "KeyY", 0x1d: "KeyZ",
  0x1e: "Digit1", 0x1f: "Digit2", 0x20: "Digit3", 0x21: "Digit4",
  0x22: "Digit5", 0x23: "Digit6", 0x24: "Digit7", 0x25: "Digit8",
  0x26: "Digit9", 0x27: "Digit0",
  0x2d: "Minus", 0x2e: "Equal",
  0x2f: "BracketLeft", 0x30: "BracketRight", 0x31: "Backslash",
  0x32: "IntlHash",
  0x33: "Semicolon", 0x34: "Quote", 0x35: "Backquote",
  0x36: "Comma", 0x37: "Period", 0x38: "Slash",
  0x64: "IntlBackslash",
  0x28: "Enter",
  0x29: "Escape",
  0x2a: "Backspace",
  0x2b: "Tab",
  0x2c: "Space",
  0x39: "CapsLock",
  0x49: "Insert",
  0x4a: "Home",
  0x4b: "PageUp",
  0x4c: "Delete",
  0x4d: "End",
  0x4e: "PageDown",
  0x4f: "ArrowRight",
  0x50: "ArrowLeft",
  0x51: "ArrowDown",
  0x52: "ArrowUp",
  0xe0: "ControlLeft",
  0xe1: "ShiftLeft",
  0xe2: "AltLeft",
  0xe3: "MetaLeft",
  0xe4: "ControlRight",
  0xe5: "ShiftRight",
  0xe6: "AltRight",
  0xe7: "MetaRight",
};

const ALIGN4_LINE_TO_SLOT = [0, 6, 2, 8] as const;

type LayoutMap = Map<string, string>;

export interface OsKeyVariantEntry {
  base?: string;
  shift?: string;
  altGr?: string;
  shiftAltGr?: string;
}

export type OsKeyVariantMap = Record<string, OsKeyVariantEntry>;

interface OsLegendCacheState {
  layoutMap: LayoutMap | null;
  osVariants: OsKeyVariantMap | null;
  initialized: boolean;
}

const osLegendCache: OsLegendCacheState = {
  layoutMap: null,
  osVariants: null,
  initialized: false,
};

const osLegendListeners = new Set<() => void>();
let osLegendInFlight: Promise<void> | null = null;

function snapshotOsLegendCache(): OsLegendCacheState {
  return {
    layoutMap: osLegendCache.layoutMap,
    osVariants: osLegendCache.osVariants,
    initialized: osLegendCache.initialized,
  };
}

function notifyOsLegendListeners(): void {
  for (const listener of osLegendListeners) {
    listener();
  }
}

async function refreshOsLegendCache(force = false): Promise<void> {
  if (osLegendInFlight && !force) {
    return osLegendInFlight;
  }

  osLegendInFlight = (async () => {
    const [nextLayoutMapRaw, nextVariantsRaw] = await Promise.all([
      getOSLayoutMap(),
      getOsKeyVariantsFromSystem(),
    ]);

    // Keep the last known good mapping to avoid visual regressions during transient refresh failures.
    const nextLayoutMap = nextLayoutMapRaw ?? osLegendCache.layoutMap;
    const nextVariants = nextVariantsRaw ?? osLegendCache.osVariants;
    const changed =
      nextLayoutMap !== osLegendCache.layoutMap ||
      nextVariants !== osLegendCache.osVariants ||
      !osLegendCache.initialized;

    osLegendCache.layoutMap = nextLayoutMap;
    osLegendCache.osVariants = nextVariants;
    osLegendCache.initialized = true;

    if (changed) {
      notifyOsLegendListeners();
    }
  })().finally(() => {
    osLegendInFlight = null;
  });

  return osLegendInFlight;
}

function useOsLegendCacheState(): OsLegendCacheState {
  const [state, setState] = useState<OsLegendCacheState>(() => snapshotOsLegendCache());

  useEffect(() => {
    const onCacheChange = () => setState(snapshotOsLegendCache());
    osLegendListeners.add(onCacheChange);

    const refresh = () => {
      void refreshOsLegendCache(true);
    };

    const handleVisibilityChange = () => {
      if (document.visibilityState === "visible") {
        refresh();
      }
    };

    void refreshOsLegendCache(false);
    window.addEventListener("focus", refresh);
    document.addEventListener("visibilitychange", handleVisibilityChange);

    return () => {
      osLegendListeners.delete(onCacheChange);
      window.removeEventListener("focus", refresh);
      document.removeEventListener("visibilitychange", handleVisibilityChange);
    };
  }, []);

  return state;
}

export interface KeycapLegend {
  primary: string;
  secondary?: string;
  lines: string[];
  slots: Array<string | undefined>;
  text: string;
  searchText: string;
}

export type KeycapLegendResolver = ((hidKeycode: number, fallbackName: string) => KeycapLegend) & {
  isReady: boolean;
};

async function getOSLayoutMap(): Promise<LayoutMap | null> {
  try {
    if ("keyboard" in navigator) {
      const kb = navigator as Navigator & { keyboard: { getLayoutMap(): Promise<LayoutMap> } };
      return await kb.keyboard.getLayoutMap();
    }
  } catch {
    // API not available or denied
  }
  return null;
}

export async function getOsKeyVariantsFromSystem(): Promise<OsKeyVariantMap | null> {
  try {
    return await invoke<OsKeyVariantMap>("kbhe_get_os_key_variants");
  } catch {
    return null;
  }
}

function normalizeOsLabel(value: string): string {
  if (value.length === 1 && /^[a-z]$/.test(value)) {
    return value.toUpperCase();
  }
  return value;
}

function buildLegendFromLines(lines: string[]): KeycapLegend {
  const normalizedLines = lines.map((line) => line.trim()).filter(Boolean);
  const safeLines = normalizedLines.length > 0 ? normalizedLines : [""];
  const slots: Array<string | undefined> = Array.from({ length: 12 }, () => "");

  for (let i = 0; i < 4; i += 1) {
    const line = lines[i]?.trim();
    if (!line) continue;
    slots[ALIGN4_LINE_TO_SLOT[i]] = line;
  }

  if (!slots.some((slot) => slot && slot.length > 0) && safeLines[0]) {
    slots[0] = safeLines[0];
  }

  return {
    primary: safeLines[0] ?? "",
    secondary: safeLines[1],
    lines: safeLines,
    slots,
    text: safeLines.join("\n"),
    searchText: safeLines.join(" ").toLowerCase(),
  };
}

function buildSimpleLegend(text: string): KeycapLegend {
  return buildLegendFromLines([text]);
}

function addUniqueLine(target: string[], value: string | undefined): void {
  if (!value) return;
  const trimmed = value.trim();
  if (!trimmed) return;
  const normalized = normalizeOsLabel(trimmed);
  if (!target.includes(normalized)) target.push(normalized);
}

function normalizeLegendValue(value: string | undefined): string | undefined {
  if (!value) return undefined;
  const trimmed = value.trim();
  if (!trimmed) return undefined;
  return normalizeOsLabel(trimmed);
}

function buildLegendFromSlots(
  slotsInput: Array<string | undefined>,
  fallbackName: string,
): KeycapLegend {
  const slots: Array<string | undefined> = Array.from({ length: 12 }, () => "");

  for (const slotIndex of [0, 6, 2, 8] as const) {
    const value = slotsInput[slotIndex];
    if (value) {
      slots[slotIndex] = value;
    }
  }

  const lines: string[] = [];
  addUniqueLine(lines, slots[0]);
  addUniqueLine(lines, slots[6]);
  addUniqueLine(lines, slots[2]);
  addUniqueLine(lines, slots[8]);

  if (lines.length === 0) {
    addUniqueLine(lines, fallbackName);
  }

  if (!slots.some((slot) => slot && slot.length > 0) && lines[0]) {
    slots[0] = lines[0];
  }

  return {
    primary: lines[0] ?? "",
    secondary: lines[1],
    lines,
    slots,
    text: lines.join("\n"),
    searchText: lines.join(" ").toLowerCase(),
  };
}

function resolvePrintableLegend(
  domCode: string,
  fallbackName: string,
  layoutMap: LayoutMap | null,
  osVariants: OsKeyVariantMap | null,
): KeycapLegend {
  const layoutBase = normalizeLegendValue(layoutMap?.get(domCode));
  const variants = osVariants?.[domCode];
  const base = layoutBase ?? normalizeLegendValue(variants?.base);
  const shift = normalizeLegendValue(variants?.shift);
  const altGr = normalizeLegendValue(variants?.altGr);
  const shiftAltGr = normalizeLegendValue(variants?.shiftAltGr);

  const slots: Array<string | undefined> = Array.from({ length: 12 }, () => undefined);
  const hasDistinctShiftLegend = Boolean(shift && shift !== base);

  if (hasDistinctShiftLegend) {
    slots[0] = shift;
  }

  if (base) {
    slots[hasDistinctShiftLegend ? 6 : 0] = base;
  }

  if (shiftAltGr && shiftAltGr !== shift && shiftAltGr !== altGr) {
    slots[2] = shiftAltGr;
  }

  if (altGr && altGr !== base) {
    slots[8] = altGr;
  }

  return buildLegendFromSlots(slots, fallbackName);
}

/**
 * Returns a function that resolves an HID keycode to an OS-specific label.
 * Falls back to the provided `fallback` name if no OS mapping is available.
 */
export function useOSLayout() {
  const { layoutMap } = useOsLegendCacheState();

  return useCallback((hidKeycode: number, fallbackName: string): string => {
    if (!layoutMap) return fallbackName;
    const domCode = HID_TO_DOM_CODE[hidKeycode];
    if (!domCode) return fallbackName;
    const osLabel = layoutMap.get(domCode);
    if (!osLabel) return fallbackName;
    return normalizeOsLabel(osLabel);
  }, [layoutMap]);
}

/**
 * Resolves an HID keycode to a keycap-like legend for UI display.
 * Printable keys can return two entries (top/bottom), e.g. "!\n1".
 */
export function useOSKeycapLegend() {
  const { layoutMap, osVariants, initialized } = useOsLegendCacheState();

  return useMemo(() => {
    const resolver = ((hidKeycode: number, fallbackName: string): KeycapLegend => {
      const domCode = HID_TO_DOM_CODE[hidKeycode];
      if (!domCode) return buildSimpleLegend(fallbackName);

      return resolvePrintableLegend(domCode, fallbackName, layoutMap, osVariants);
    }) as KeycapLegendResolver;

    resolver.isReady = initialized;
    return resolver;
  }, [initialized, layoutMap, osVariants]);
}
