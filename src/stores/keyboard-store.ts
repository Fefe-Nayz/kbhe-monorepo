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
  setSelectedKeys: (keyIds: string[]) => void;
  toggleKeySelection: (keyId: string) => void;
  clearSelection: () => void;
  selectAll: () => void;
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
  setSelectedKeys: (keyIds) => set({ selectedKeys: [...keyIds] }),

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
  selectAll: () => set({ selectedKeys: Array.from({ length: 82 }, (_, i) => `key-${i}`) }),

  updateKeyConfig: (keyIds, update) => {
    set((state) => {
      const { bindings, rotaryBindings } = state.layout;
      let nextBindings = bindings;
      let nextRotaryBindings = rotaryBindings;
      let changed = false;

      keyIds.forEach((keyId) => {
        if (keyId in bindings) {
          const current = bindings[keyId];
          const nextConfig = {
            ...current,
            ...update,
            label: update.label ? [...update.label] : current.label,
          };

          if (
            current.value === nextConfig.value &&
            current.color === nextConfig.color &&
            current.fontSize === nextConfig.fontSize &&
            current.label === nextConfig.label
          ) {
            return;
          }

          if (nextBindings === bindings) {
            nextBindings = { ...bindings };
          }

          nextBindings[keyId] = nextConfig;
          changed = true;
          return;
        }

        if (keyId in rotaryBindings) {
          const rotaryKey = keyId as keyof typeof rotaryBindings;
          const current = rotaryBindings[rotaryKey];
          const nextConfig = {
            ...current,
            ...update,
            label: update.label ? [...update.label] : current.label,
          };

          if (
            current.value === nextConfig.value &&
            current.color === nextConfig.color &&
            current.fontSize === nextConfig.fontSize &&
            current.label === nextConfig.label
          ) {
            return;
          }

          if (nextRotaryBindings === rotaryBindings) {
            nextRotaryBindings = { ...rotaryBindings };
          }

          nextRotaryBindings[rotaryKey] = nextConfig;
          changed = true;
        }
      });

      if (!changed) {
        return state;
      }

      return {
        layout: {
          bindings: nextBindings,
          rotaryBindings: nextRotaryBindings,
        },
      };
    });
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
