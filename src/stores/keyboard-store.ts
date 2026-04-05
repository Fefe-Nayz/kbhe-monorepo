import { create } from 'zustand'
import { defaultLayout, type KeyboardLayout, type KeyMode, type displayedInfo, type labelItems} from '@/constants/defaultLayout'

//import { useProfileStore } from './profileStore'

//export type KeyMode = 'single' | 'multi'
//export type displayedInfo = "regular" | "actuationMode" | "analogValues"
//export type labelItems = string

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
  saveEnabled: boolean

  setMode: (mode: KeyMode) => void
  setDisplayedInfo: (displayedInfo: displayedInfo) => void
  toggleKeySelection: (keyId: string) => void
  clearSelection: () => void
  updateKeyConfig: (keyIds: string[], update: Partial<KeyConfig>) => void
  updateLayout: (layout: KeyboardLayout) => void
  resetLayout: (save?: boolean) => void
  setSaveEnabled: (enabled: boolean) => void 
}

export const useKeyboardStore = create<KeyboardState>()(
    (set, get) => ({

      mode: "single",
      displayedInfo: "regular",
      selectedKeys: [],
      layout: defaultLayout,
      saveEnabled: false, // we don't save by default

      setSaveEnabled: (enabled) => set({ saveEnabled: enabled }),

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

      updateKeyConfig: (keyIds: string[], update: Partial<KeyConfig>) => {
        const { layout } = get()

        const newLayout = {
          keys: layout.keys.map(row =>
            row.map(key =>
              keyIds.includes(key.id)
                ? { ...key, ...update }
                : key
            )
          )
        }

        set({ layout: newLayout })
        
      },

      updateLayout: (layout) => set({ layout }),

      resetLayout: (save = true) => {
        const { setSaveEnabled } = get()
        if (!save) setSaveEnabled(false)
        set({ layout: defaultLayout, selectedKeys: [] })
        if (!save) setSaveEnabled(true)
      }

    }),
    
)

