import { create } from "zustand"
import { useKeyboardStore ,type KeyboardState } from "./baseKeyboardStore"


{/*
interface KeyboardProfile {
  name: string
  data: KeyboardState
}

interface ProfileStore {
  profiles: KeyboardProfile[]
  refresh: () => void
  load: (name: string) => void
  remove: (name: string) => void
  duplicate: (from: string, to: string) => void
}

export const useProfileStore = create<ProfileStore>((set) => ({
  profiles: [],

  refresh: () => {
    const result: KeyboardProfile[] = []

    Object.keys(localStorage).forEach(key => {
      if (!key.startsWith("keyboard-storage")) return

      const raw = localStorage.getItem(key)
      if (!raw) return

      try {
        const parsed: { state: KeyboardState } = JSON.parse(raw)

        result.push({
          name: key,
          data: parsed.state
        })
      } catch {}
    })

    set({ profiles: result })
  },

  load: (name) => {
    const raw = localStorage.getItem(name)
    if (!raw) return

    const parsed: { state: KeyboardState } = JSON.parse(raw)

    // injection directe dans ton store clavier
    useKeyboardStore.setState(parsed.state, true)
  },

  remove: (name) => {
    localStorage.removeItem(name)
    set(state => ({
      profiles: state.profiles.filter(p => p.name !== name)
    }))
  },

  duplicate: (from, to) => {
    const raw = localStorage.getItem(from)
    if (!raw) return
    localStorage.setItem(to, raw)
  }
}))
*/}