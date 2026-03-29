import { create } from 'zustand'
import { persist, createJSONStorage } from 'zustand/middleware'
import { defaultLayout, type KeyboardLayout } from '@/constants/defaultLayout'

export type KeyMode = 'single' | 'multi'
export type displayedInfo = "regular" | "actuationMode" | "analogValues"
export type labelItems = string

export interface KeyConfig {
  id: string
  label: labelItems[]
  value: string
  width: number
  gap: number
  color?: string
  fontSize?: number
}

export interface KeyboardState {

  mode: KeyMode
  displayedInfo: displayedInfo
  selectedKeys: string[]
  layout: KeyboardLayout

  setMode: (mode: KeyMode) => void
  setDisplayedInfo: (displayedInfo: displayedInfo) => void
  toggleKeySelection: (keyId: string) => void
  clearSelection: () => void
  updateKeyConfig: (keyIds: string[], update: string) => void
  updateLayout: (layout: KeyboardLayout) => void
  resetLayout: () => void
}

export const useKeyboardStore = create<KeyboardState>()(
  persist(
    (set, get) => ({

      mode: "single",
      displayedInfo: "regular",
      selectedKeys: [],
      layout: defaultLayout,

      setMode: (mode) => set({ mode }),

      setDisplayedInfo: (displayedInfo) => set({ displayedInfo }),

      toggleKeySelection: (keyId) => {

        const { mode, selectedKeys } = get()

        if (mode === "single") {

          set({
            selectedKeys: selectedKeys.includes(keyId) ? [] : [keyId]
          })

        } else {

          set({
            selectedKeys: selectedKeys.includes(keyId)
              ? selectedKeys.filter(id => id !== keyId)
              : [...selectedKeys, keyId]
          })

        }
      },

      clearSelection: () => set({ selectedKeys: [] }),

      updateKeyConfig: (keyIds, update) => {

        const { layout } = get()

        const newLayout = {
          keys: layout.keys.map(row =>
            row.map(key =>
              keyIds.includes(key.id)
                ? { ...key, value: update }
                : key
            )
          )
        }

        set({ layout: newLayout })
      },

      updateLayout: (layout) => set({ layout }),

      resetLayout: () => set({
        layout: defaultLayout,
        selectedKeys: []
      })

    }),
    {
      name: "keyboard-storage",

      storage: createJSONStorage(() => localStorage),

      partialize: (state) => ({
        layout: state.layout,
        mode: state.mode
      })
    }
  )
)