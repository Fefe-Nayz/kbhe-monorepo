import { create } from "zustand"
import { useKeyboardStore, type KeyboardState } from "./keyboard-store"
import { cloneDefaultLayout, normalizeKeyboardLayout } from "@/constants/defaultLayout"

const STORAGE_PREFIX = "keyboard-profile:"

const DEFAULT_PROFILE_NAME = "default"

const ACTIVE_PROFILE = "keyboard-active-profile"

interface KeyboardProfile {
  name: string
  data: KeyboardState
}

interface ProfileStore {
  profiles: KeyboardProfile[]

  getNumberOfProfiles: () => number
  init: () => void
  refresh: () => void
  selectedProfile: KeyboardProfile | null
  selectProfile: (name: string) => void
  save: (name: string) => void
  remove: (name: string) => void
  rename: (oldName: string, newName: string) => void
  duplicate: (from: string, to: string) => void
  updateSelectedProfile: (data: KeyboardState) => void
}

export const useProfileStore = create<ProfileStore>((set, get) => ({

  profiles: [],
  selectedProfile: null,

  getNumberOfProfiles: () => get().profiles.length,

  init: () => {
  const key = STORAGE_PREFIX + DEFAULT_PROFILE_NAME

  // Is there a default profile in localStorage
  const existing = localStorage.getItem(key)

  if (!existing) {

    const defaultState = useKeyboardStore.getState()

    localStorage.setItem(
      key,
      JSON.stringify(defaultState)
    )

  }
  // load profiles from localStorage
  get().refresh()

  const savedProfile = localStorage.getItem(ACTIVE_PROFILE)

  if (savedProfile) {

    const profileExists = get().profiles.find(p => p.name === savedProfile)

    if (profileExists) {
      get().selectProfile(savedProfile)
      return
    }

  }

    // If no active profile or active profile doesn't exist → select default profile
  get().selectProfile(DEFAULT_PROFILE_NAME)

},

  refresh: () => {
    const profiles: KeyboardProfile[] = []

    for (const key of Object.keys(localStorage)) {

      if (!key.startsWith(STORAGE_PREFIX)) continue

      const raw = localStorage.getItem(key)
      if (!raw) continue

      try {
        const data: KeyboardState = JSON.parse(raw)
        data.layout = normalizeKeyboardLayout(data.layout)
        data.currentLayer = data.currentLayer ?? 0

        profiles.push({
          name: key.replace(STORAGE_PREFIX, ""),
          data
        })

      } catch (e) {
        console.warn("Invalid profile:", key)
      }
    }

    set({ profiles })
  },


  save: (name) => {

    let state = useKeyboardStore.getState()

    // When a new profile is created, initialize it with the default layout
    if (!get().profiles.find(p => p.name === name)) {
      state = { ...state, layout: cloneDefaultLayout() }
    }

    const dataToSave = {
      layout: state.layout,
      mode: state.mode,
      displayedInfo: state.displayedInfo,
      currentLayer: state.currentLayer,
    }
    localStorage.setItem(STORAGE_PREFIX + name, JSON.stringify(dataToSave))
    get().refresh()

    //When saving a new profile, automatically select it
    get().selectProfile(name)
  },

  remove: (name) => {

  if (name === DEFAULT_PROFILE_NAME) {
    console.warn("Default profile cannot be deleted")
    return
  }

  localStorage.removeItem(STORAGE_PREFIX + name)

  const active = localStorage.getItem(ACTIVE_PROFILE)

  // if the deleted profile was active, switch to default profile
  if (active === name) {

    localStorage.setItem(ACTIVE_PROFILE, DEFAULT_PROFILE_NAME)

    get().selectProfile(DEFAULT_PROFILE_NAME)
  }

  get().refresh()
},

  rename: (oldName, newName) => {
    if (oldName === DEFAULT_PROFILE_NAME) return;
    if (!newName.trim() || oldName === newName) return;
    if (get().profiles.find((p) => p.name === newName)) return;

    const raw = localStorage.getItem(STORAGE_PREFIX + oldName);
    if (!raw) return;

    localStorage.setItem(STORAGE_PREFIX + newName, raw);
    localStorage.removeItem(STORAGE_PREFIX + oldName);

    const active = localStorage.getItem(ACTIVE_PROFILE);
    if (active === oldName) {
      localStorage.setItem(ACTIVE_PROFILE, newName);
    }

    get().refresh();

    if (get().selectedProfile?.name === oldName || active === oldName) {
      get().selectProfile(newName);
    }
  },

  duplicate: (from, to) => {

    const raw = localStorage.getItem(STORAGE_PREFIX + from)
    if (!raw) return

    localStorage.setItem(STORAGE_PREFIX + to, raw)

    get().refresh()
  },
  selectProfile: (name) => {

  const profile = get().profiles.find(p => p.name === name)

  if (!profile) return

  set({ selectedProfile: profile })

  // apply to keyboard store
  useKeyboardStore.setState({
    layout: normalizeKeyboardLayout(profile.data.layout),
    currentLayer: profile.data.currentLayer ?? 0,
  })

  localStorage.setItem(ACTIVE_PROFILE, name)
},
  /*updateSelectedProfile: (data) => {

    const { selectedProfile } = get()

    if (!selectedProfile) return

    localStorage.setItem(STORAGE_PREFIX + selectedProfile.name, JSON.stringify(data))
    get().refresh()
  }*/
 updateSelectedProfile: (data) => {
  const { selectedProfile } = get()
  if (!selectedProfile) return

  const dataToSave = {
    layout: data.layout,
    mode: data.mode,
    displayedInfo: data.displayedInfo,
    currentLayer: data.currentLayer,
  }

  localStorage.setItem(
    STORAGE_PREFIX + selectedProfile.name,
    JSON.stringify(dataToSave)
  )
  get().refresh()
}

}))
