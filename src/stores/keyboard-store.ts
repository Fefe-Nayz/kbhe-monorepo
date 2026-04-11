import { create } from "zustand";
import {
  cloneDefaultLayout,
  type displayedInfo,
  type KeyConfig,
  type KeyboardLayout,
  type KeyMode,
  normalizeKeyboardLayout,
} from "@/constants/defaultLayout";

export interface KeyboardState {
  mode: KeyMode;
  displayedInfo: displayedInfo;
  selectedKeys: string[];
  layout: KeyboardLayout;
  saveEnabled: boolean;
  currentLayer: number;

  setMode: (mode: KeyMode) => void;
  setDisplayedInfo: (displayedInfo: displayedInfo) => void;
  setCurrentLayer: (layer: number) => void;
  toggleKeySelection: (keyId: string) => void;
  clearSelection: () => void;
  updateKeyConfig: (keyIds: string[], update: Partial<KeyConfig>) => void;
  updateLayout: (layout: KeyboardLayout) => void;
  resetLayout: (save?: boolean) => void;
  setSaveEnabled: (enabled: boolean) => void;
}

function cloneLayout(layout: KeyboardLayout): KeyboardLayout {
  return normalizeKeyboardLayout(layout);
}

export const useKeyboardStore = create<KeyboardState>()((set, get) => ({
  mode: "single",
  displayedInfo: "regular",
  selectedKeys: [],
  layout: cloneDefaultLayout(),
  saveEnabled: false,
  currentLayer: 0,

  setSaveEnabled: (enabled) => set({ saveEnabled: enabled }),
  setMode: (mode) => set({ mode }),
  setDisplayedInfo: (displayedInfo) => set({ displayedInfo }),
  setCurrentLayer: (layer) => set({ currentLayer: Math.max(0, Math.min(3, Math.trunc(layer))) }),

  toggleKeySelection: (keyId) => {
    const { mode, selectedKeys } = get();
    if (mode === "single") {
      set({
        selectedKeys: selectedKeys.includes(keyId) ? [] : [keyId],
      });
      return;
    }

    set({
      selectedKeys: selectedKeys.includes(keyId)
        ? selectedKeys.filter((id) => id !== keyId)
        : [...selectedKeys, keyId],
    });
  },

  clearSelection: () => set({ selectedKeys: [] }),

  updateKeyConfig: (keyIds, update) => {
    const { layout } = get();
    const next = cloneLayout(layout);

    keyIds.forEach((keyId) => {
      if (keyId in next.bindings) {
        next.bindings[keyId] = {
          ...next.bindings[keyId],
          ...update,
          label: update.label ? [...update.label] : next.bindings[keyId].label,
        };
        return;
      }

      if (keyId in next.rotaryBindings) {
        const rotaryKey = keyId as keyof typeof next.rotaryBindings;
        next.rotaryBindings[rotaryKey] = {
          ...next.rotaryBindings[rotaryKey],
          ...update,
          label: update.label ? [...update.label] : next.rotaryBindings[rotaryKey].label,
        };
      }
    });

    set({ layout: next });
  },

  updateLayout: (layout) => set({ layout: cloneLayout(layout) }),

  resetLayout: (save = true) => {
    const { setSaveEnabled } = get();
    if (!save) {
      setSaveEnabled(false);
    }
    set({ layout: cloneDefaultLayout(), selectedKeys: [] });
    if (!save) {
      setSaveEnabled(true);
    }
  },
}));
