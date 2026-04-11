/**
 * UI-only ephemeral state — never persisted, never sent to device.
 * Kept separate from keyboard-store (which owns layout/profile state).
 */

import { create } from "zustand";

export interface UiState {
  /** Keys selected in the keyboard preview */
  selectedKeys: string[];
  /** Single focused key (last clicked) */
  focusedKey: string | null;
  /** Layer currently shown in the preview */
  activeLayer: number;
  /** Which rotary target is selected: "press" | "ccw" | "cw" | null */
  rotaryTarget: "press" | "ccw" | "cw" | null;
  /** Whether the action-picker panel is open */
  actionPickerOpen: boolean;
  /** Page-level panel open states */
  panels: Record<string, boolean>;

  setSelectedKeys: (keys: string[]) => void;
  toggleKey: (keyId: string, multi?: boolean) => void;
  clearSelection: () => void;
  setFocusedKey: (keyId: string | null) => void;
  setActiveLayer: (layer: number) => void;
  setRotaryTarget: (t: "press" | "ccw" | "cw" | null) => void;
  setActionPickerOpen: (open: boolean) => void;
  setPanel: (name: string, open: boolean) => void;
}

export const useUiStore = create<UiState>()((set, get) => ({
  selectedKeys: [],
  focusedKey: null,
  activeLayer: 0,
  rotaryTarget: null,
  actionPickerOpen: false,
  panels: {},

  setSelectedKeys: (selectedKeys) => set({ selectedKeys }),

  toggleKey: (keyId, multi = false) => {
    const { selectedKeys } = get();
    if (!multi) {
      const next = selectedKeys.length === 1 && selectedKeys[0] === keyId ? [] : [keyId];
      set({ selectedKeys: next, focusedKey: next.length ? keyId : null });
      return;
    }
    const already = selectedKeys.includes(keyId);
    const next = already ? selectedKeys.filter((k) => k !== keyId) : [...selectedKeys, keyId];
    set({ selectedKeys: next, focusedKey: already ? get().focusedKey : keyId });
  },

  clearSelection: () => set({ selectedKeys: [], focusedKey: null }),
  setFocusedKey: (focusedKey) => set({ focusedKey }),
  setActiveLayer: (layer) => set({ activeLayer: Math.max(0, Math.min(3, layer)) }),
  setRotaryTarget: (rotaryTarget) => set({ rotaryTarget }),
  setActionPickerOpen: (actionPickerOpen) => set({ actionPickerOpen }),
  setPanel: (name, open) => set((s) => ({ panels: { ...s.panels, [name]: open } })),
}));
