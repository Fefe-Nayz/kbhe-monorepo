import { useCallback, useEffect, useState } from "react";

/**
 * Maps DOM KeyboardEvent.code → HID keycode (subset for printable keys).
 * Reversed: HID keycode → DOM code, so we can look up the OS label.
 */
const HID_TO_DOM_CODE: Record<number, string> = {
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
  0x33: "Semicolon", 0x34: "Quote", 0x35: "Backquote",
  0x36: "Comma", 0x37: "Period", 0x38: "Slash",
  0x64: "IntlBackslash",
};

type LayoutMap = Map<string, string>;

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

/**
 * Returns a function that resolves an HID keycode to an OS-specific label.
 * Falls back to the provided `fallback` name if no OS mapping is available.
 */
export function useOSLayout() {
  const [layoutMap, setLayoutMap] = useState<LayoutMap | null>(null);

  useEffect(() => {
    void getOSLayoutMap().then(setLayoutMap);
  }, []);

  return useCallback((hidKeycode: number, fallbackName: string): string => {
    if (!layoutMap) return fallbackName;
    const domCode = HID_TO_DOM_CODE[hidKeycode];
    if (!domCode) return fallbackName;
    const osLabel = layoutMap.get(domCode);
    if (!osLabel) return fallbackName;
    return osLabel.length === 1 ? osLabel.toUpperCase() : osLabel;
  }, [layoutMap]);
}
