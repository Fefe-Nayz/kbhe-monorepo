import { create } from 'zustand';
import { persist, createJSONStorage } from 'zustand/middleware';


//import { invoke } from '@tauri-apps/api/core';

// Types
export type KeyMode = 'single' | 'multi';
export type displayedInfo = "regular" | "actuationMode" | "analogValues"
export type labelItems = React.ReactNode;

export interface KeyConfig {
  id: string;
  label: labelItems[];
  value: string;
  width: number;
  gap: number;
  color?: string;
  fontSize?: number;
}

export interface KeyboardLayout {
  keys: KeyConfig[][];
}

export interface KeyboardState {
  
  mode: KeyMode;
  displayedInfo: displayedInfo;
  selectedKeys: string[]; // Selected keys IDs
  layout: KeyboardLayout;
  
  // Actions
  setMode: (mode: KeyMode) => void;
  setDisplayedInfo: (displayedInfo: displayedInfo) => void;
  toggleKeySelection: (keyId: string) => void;
  clearSelection: (selectedKeys: string[]) => void;
  updateKeyConfig: (keyId: string[], updates: string) => void;
  updateLayout: (layout: KeyboardLayout) => void;
  resetLayout: (defaultLayout: KeyboardLayout) => void;
}


const defaultLayout: KeyboardLayout = {
  keys: [
    [
      { id: "esc", label: ["Esc"], value: "Escape", width: 1, gap: 0 },
      { id: "f1", label: ["F1"], value: "F1", width: 1, gap: 0 },
      { id: "f2", label: ["F2"], value: "F2", width: 1, gap: 0 },
      { id: "f3", label: ["F3"], value: "F3", width: 1, gap: 0 },
      { id: "f4", label: ["F4"], value: "F4", width: 1, gap: 0 },
      { id: "f5", label: ["F5"], value: "F5", width: 1, gap: 0 },
      { id: "f6", label: ["F6"], value: "F6", width: 1, gap: 0 },
      { id: "f7", label: ["F7"], value: "F7", width: 1, gap: 0 },
      { id: "f8", label: ["F8"], value: "F8", width: 1, gap: 0 },
      { id: "f9", label: ["F9"], value: "F9", width: 1, gap: 0 },
      { id: "f10", label: ["F10"], value: "F10", width: 1, gap: 0 },
      { id: "f11", label: ["F11"], value: "F11", width: 1, gap: 0 },
      { id: "f12", label: ["F12"], value: "F12", width: 1, gap: 0 },
      { id: "del", label: ["Del"], value: "Delete", width: 1, gap: 0 },
    ],

    [
      { id: "grave", label: ["²"], value: "Backquote", width: 1, gap: 0 },
      { id: "1", label: ["&"], value: "Digit1", width: 1, gap: 0 },
      { id: "2", label: ["é"], value: "Digit2", width: 1, gap: 0 },
      { id: "3", label: ['"'], value: "Digit3", width: 1, gap: 0 },
      { id: "4", label: ["'"], value: "Digit4", width: 1, gap: 0 },
      { id: "5", label: ["("], value: "Digit5", width: 1, gap: 0 },
      { id: "6", label: ["-"], value: "Digit6", width: 1, gap: 0 },
      { id: "7", label: ["è"], value: "Digit7", width: 1, gap: 0 },
      { id: "8", label: ["_"], value: "Digit8", width: 1, gap: 0 },
      { id: "9", label: ["ç"], value: "Digit9", width: 1, gap: 0 },
      { id: "0", label: ["à"], value: "Digit0", width: 1, gap: 0 },
      { id: "minus", label: [")"], value: "Minus", width: 1, gap: 0 },
      { id: "equal", label: ["="], value: "Equal", width: 1, gap: 0 },
      { id: "backspace", label: ["Backspace"], value: "Backspace", width: 2, gap: 0 },
      { id: "pgup", label: ["PgUp"], value: "PageUp", width: 1, gap: 14 },
    ],

    [
      { id: "tab", label: ["Tab"], value: "Tab", width: 1.5, gap: 0 },
      { id: "a", label: ["A"], value: "KeyA", width: 1, gap: 0 },
      { id: "z", label: ["Z"], value: "KeyZ", width: 1, gap: 0 },
      { id: "e", label: ["E"], value: "KeyE", width: 1, gap: 0 },
      { id: "r", label: ["R"], value: "KeyR", width: 1, gap: 0 },
      { id: "t", label: ["T"], value: "KeyT", width: 1, gap: 0 },
      { id: "y", label: ["Y"], value: "KeyY", width: 1, gap: 0 },
      { id: "u", label: ["U"], value: "KeyU", width: 1, gap: 0 },
      { id: "i", label: ["I"], value: "KeyI", width: 1, gap: 0 },
      { id: "o", label: ["O"], value: "KeyO", width: 1, gap: 0 },
      { id: "p", label: ["P"], value: "KeyP", width: 1, gap: 0 },
      { id: "lbracket", label: ["^"], value: "BracketLeft", width: 1, gap: 0 },
      { id: "rbracket", label: ["$"], value: "BracketRight", width: 1, gap: 0 },
      { id: "enter", label: ["Enter"], value: "Enter", width: 1.3, gap: 0 },
      { id: "pgdn", label: ["PgDn"], value: "PageDown", width: 1, gap: 32 },
    ],

    [
      { id: "capslock", label: ["Caps"], value: "CapsLock", width: 1.75, gap: 0 },
      { id: "q", label: ["Q"], value: "KeyQ", width: 1, gap: 0 },
      { id: "s", label: ["S"], value: "KeyS", width: 1, gap: 0 },
      { id: "d", label: ["D"], value: "KeyD", width: 1, gap: 0 },
      { id: "f", label: ["F"], value: "KeyF", width: 1, gap: 0 },
      { id: "g", label: ["G"], value: "KeyG", width: 1, gap: 0 },
      { id: "h", label: ["H"], value: "KeyH", width: 1, gap: 0 },
      { id: "j", label: ["J"], value: "KeyJ", width: 1, gap: 0 },
      { id: "k", label: ["K"], value: "KeyK", width: 1, gap: 0 },
      { id: "l", label: ["L"], value: "KeyL", width: 1, gap: 0 },
      { id: "m", label: ["M"], value: "KeyM", width: 1, gap: 0 },
      { id: "ù", label: ["Ù"], value: "Quote", width: 1, gap: 0 },
      { id: "backslash", label: ["*"], value: "Backslash", width: 1, gap: 0 },
      { id: "home", label: ["Home"], value: "Home", width: 1, gap: 30 },
    ],

    [
      { id: "shift", label: ["Shift"], value: "ShiftLeft", width: 2.25, gap: 0 },
      { id: "sign", label: ["<"], value: "sign", width: 1, gap: 0 },
      { id: "w", label: ["W"], value: "KeyW", width: 1, gap: 0 },
      { id: "x", label: ["X"], value: "KeyX", width: 1, gap: 0 },
      { id: "c", label: ["C"], value: "KeyC", width: 1, gap: 0 },
      { id: "v", label: ["V"], value: "KeyV", width: 1, gap: 0 },
      { id: "b", label: ["B"], value: "KeyB", width: 1, gap: 0 },
      { id: "n", label: ["N"], value: "KeyN", width: 1, gap: 0 },
      { id: "comma", label: [","], value: "Comma", width: 1, gap: 0 },
      { id: "semicolon", label: [";"], value: "Semicolon", width: 1, gap: 0 },
      { id: "colon", label: [":"], value: "Period", width: 1, gap: 0 },
      { id: "excl", label: ["!"], value: "Slash", width: 1, gap: 0 },
      { id: "shift_r", label: ["Shift"], value: "ShiftRight", width: 1.8, gap: 0 },
      { id: "up", label: ["↑"], value: "ArrowUp", width: 1, gap: 0 },
    ],

    [
      { id: "ctrl", label: ["Ctrl"], value: "ControlLeft", width: 1.25, gap: 0 },
      { id: "win", label: ["win"], value: "win", width: 1.25, gap: 0 },
      { id: "alt", label: ["Alt"], value: "AltLeft", width: 1.25, gap: 0 },
      { id: "space", label: [" "], value: "Space", width: 6.25, gap: 0 },
      { id: "altgr", label: ["AltGr"], value: "AltRight", width: 1.25, gap: 0 },
      { id: "fn", label: ["Fn"], value: "Fn", width: 1.25, gap: 0.5 },
      { id: "ctrl_r", label: ["Ctrl"], value: "ControlRight", width: 1.25, gap: 0 },
      { id: "left", label: ["←"], value: "ArrowLeft", width: 1, gap: 3.2 },
      { id: "down", label: ["↓"], value: "ArrowDown", width: 1, gap: 0 },
      { id: "right", label: ["→"], value: "ArrowRight", width: 1, gap: 0 },
    ],
  ],
};



export const useKeyboardStore = create<KeyboardState>()(
  persist(
    (set, get) => ({
      mode: 'single',
      displayedInfo: 'regular',
      selectedKeys: [],
      layout: defaultLayout,

      setMode: (mode) => set({ mode }),
      setDisplayedInfo: (displayedInfo) => set({ displayedInfo }),

      toggleKeySelection: (keyId) => {
        const { mode, selectedKeys } = get();
        console.log(mode, selectedKeys);
        /*
        const { mode, selectedKeys } = get();
        if (mode === 'single') {
          set({ selectedKeys: selectedKeys.includes(keyId) ? [] : [keyId] });
        } else {
          set({
            selectedKeys: selectedKeys.includes(keyId)
              ? selectedKeys.filter((id) => id !== keyId)
              : [...selectedKeys, keyId],
          });
        }*/
       console.log(keyId)
      },

      clearSelection: () => set({ selectedKeys: [] }),


      updateKeyConfig: (keyId, update) => {
        /*
        const { layout } = get();
        const newKeys = layout.keys.map((item) =>
          item.map((key) => (key.id === keyId ? key.label = update : key))
        );
        set({ layout: { ...newKeys} });*/

        console.log(keyId, update)
        //invoke("update_key")
      },

      updateLayout: (layout) => set({ layout }),

      resetLayout: (defaultLayout) => set({
        layout: defaultLayout,
        selectedKeys: [],
      }),
    }),
    {
      name: 'keyboard-storage',
      storage: createJSONStorage(() => localStorage),
    
    }
  )
);