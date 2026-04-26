import { create } from "zustand"
import { useKeyboardStore, type KeyboardState } from "./keyboard-store"
import { cloneDefaultLayout, normalizeKeyboardLayout } from "@/constants/defaultLayout"
import {
  isFirmwareProfileSnapshot,
  type FirmwareProfileSnapshot,
} from "@/lib/kbhe/profile-sync"

const APP_STORAGE_PREFIX = "keyboard-profile:"
const DEVICE_STORAGE_PREFIX = "keyboard-device-profile:"
const ACTIVE_APP_PROFILE_KEY = "keyboard-active-app-profile"
const LEGACY_ACTIVE_APP_PROFILE_KEY = "keyboard-active-profile"

export type RuntimeSource = "device" | "app"

export interface KeyboardProfileData {
  layout: KeyboardState["layout"]
  mode: KeyboardState["mode"]
  displayedInfo: KeyboardState["displayedInfo"]
  currentLayer: number
  firmwareSnapshot?: FirmwareProfileSnapshot
}

export interface KeyboardProfile {
  name: string
  data: KeyboardProfileData
}

export interface AppProfile extends KeyboardProfile {
  source: "app"
  id: string
}

export interface DeviceProfileRef {
  source: "device"
  slot: number
  name: string
  used: boolean
  isActive: boolean
  isDefault: boolean
  mirroredAt?: number
  firmwareSnapshot?: FirmwareProfileSnapshot
}

interface DeviceProfileMirror {
  source: "device"
  slot: number
  name: string
  used: boolean
  mirroredAt: number
  firmwareSnapshot?: FirmwareProfileSnapshot
}

interface RuntimeDeviceState {
  activeDeviceSlot?: number | null
  defaultDeviceSlot?: number | null
  ramOnlyActive?: boolean
  profileUsedMask?: number
  deviceProfiles?: Array<{
    slot: number
    name: string
    used: boolean
  }>
}

interface ProfileStore {
  // Runtime model
  appProfiles: AppProfile[]
  deviceProfiles: DeviceProfileRef[]
  runtimeSource: RuntimeSource
  activeAppProfileName: string | null
  activeDeviceSlot: number | null
  defaultDeviceSlot: number | null
  profileUsedMask: number
  ramOnlyActive: boolean

  // Legacy compatibility for existing UI surface
  profiles: KeyboardProfile[]
  selectedProfile: KeyboardProfile | null

  // Runtime controls
  setRuntimeSource: (source: RuntimeSource) => void
  setRuntimeDeviceState: (next: RuntimeDeviceState) => void
  setDeviceProfiles: (profiles: Array<{ slot: number; name: string; used: boolean }>) => void
  setActiveDeviceSlot: (slot: number | null) => void
  setDefaultDeviceSlot: (slot: number | null) => void
  setRamOnlyActive: (active: boolean) => void
  upsertDeviceProfileMirror: (
    slot: number,
    name: string,
    used: boolean,
    firmwareSnapshot?: FirmwareProfileSnapshot | null,
  ) => void
  removeDeviceProfileMirror: (slot: number) => void

  // Legacy app-profile API
  getNumberOfProfiles: () => number
  init: () => void
  refresh: () => void
  selectProfile: (name: string) => void
  save: (name: string, options?: { activate?: boolean; firmwareSnapshot?: FirmwareProfileSnapshot | null }) => void
  remove: (name: string) => void
  rename: (oldName: string, newName: string) => void
  duplicate: (from: string, to: string) => void
  getAppProfileByName: (name: string) => AppProfile | null
  upsertAppProfileData: (
    name: string,
    data: Partial<KeyboardProfileData> | KeyboardState,
    options?: { activate?: boolean; firmwareSnapshot?: FirmwareProfileSnapshot | null },
  ) => void
  updateSelectedProfile: (data: KeyboardState) => void
}

function toStoredProfileData(
  data: Partial<KeyboardProfileData> | KeyboardState,
  options?: { firmwareSnapshot?: FirmwareProfileSnapshot | null },
): KeyboardProfileData {
  const directSnapshot = isFirmwareProfileSnapshot((data as Partial<KeyboardProfileData>).firmwareSnapshot)
    ? (data as Partial<KeyboardProfileData>).firmwareSnapshot
    : undefined
  const selectedSnapshot = options?.firmwareSnapshot === null
    ? undefined
    : options?.firmwareSnapshot ?? directSnapshot

  return {
    layout: normalizeKeyboardLayout(data.layout ?? cloneDefaultLayout()),
    mode: data.mode ?? "single",
    displayedInfo: data.displayedInfo ?? "regular",
    currentLayer: Math.max(0, Math.min(3, Math.trunc(data.currentLayer ?? 0))),
    ...(selectedSnapshot ? { firmwareSnapshot: selectedSnapshot } : {}),
  }
}

function toLegacyProfile(profile: AppProfile): KeyboardProfile {
  return {
    name: profile.name,
    data: profile.data,
  }
}

function readProfileData(raw: string): KeyboardProfileData | null {
  try {
    const parsed = JSON.parse(raw) as unknown
    if (!parsed || typeof parsed !== "object") {
      return null
    }

    const candidate = "data" in parsed
      ? (parsed as { data?: unknown }).data
      : parsed

    if (!candidate || typeof candidate !== "object") {
      return null
    }

    return toStoredProfileData(candidate as Partial<KeyboardProfileData>)
  } catch {
    return null
  }
}

function loadAppProfilesFromStorage(): AppProfile[] {
  const profiles: AppProfile[] = []

  for (const key of Object.keys(localStorage)) {
    if (!key.startsWith(APP_STORAGE_PREFIX)) continue

    const raw = localStorage.getItem(key)
    if (!raw) continue

    const data = readProfileData(raw)
    if (!data) continue

    const name = key.replace(APP_STORAGE_PREFIX, "")
    if (!name) continue

    profiles.push({
      id: name,
      name,
      source: "app",
      data,
    })
  }

  return profiles
}

function readDeviceProfileMirror(slot: number): DeviceProfileMirror | null {
  const raw = localStorage.getItem(DEVICE_STORAGE_PREFIX + slot)
  if (!raw) {
    return null
  }

  try {
    const parsed = JSON.parse(raw) as Partial<DeviceProfileMirror>
    if (parsed.source !== "device" || parsed.slot !== slot || typeof parsed.name !== "string") {
      return null
    }

    return {
      source: "device",
      slot,
      name: parsed.name,
      used: Boolean(parsed.used),
      mirroredAt: typeof parsed.mirroredAt === "number" ? parsed.mirroredAt : 0,
      ...(isFirmwareProfileSnapshot(parsed.firmwareSnapshot)
        ? { firmwareSnapshot: parsed.firmwareSnapshot }
        : {}),
    }
  } catch {
    return null
  }
}

function writeDeviceProfileMirror(
  slot: number,
  name: string,
  used: boolean,
  firmwareSnapshot?: FirmwareProfileSnapshot | null,
) {
  const existing = readDeviceProfileMirror(slot)
  const snapshot = firmwareSnapshot === undefined
    ? existing?.firmwareSnapshot
    : firmwareSnapshot ?? undefined

  const record: DeviceProfileMirror = {
    source: "device",
    slot,
    name,
    used,
    mirroredAt: Date.now(),
    ...(snapshot ? { firmwareSnapshot: snapshot } : {}),
  }

  localStorage.setItem(DEVICE_STORAGE_PREFIX + slot, JSON.stringify(record))
}

function applyDeviceProfileFlags(
  profiles: DeviceProfileRef[],
  activeDeviceSlot: number | null,
  defaultDeviceSlot: number | null,
): DeviceProfileRef[] {
  return profiles.map((profile) => ({
    ...profile,
    isActive: activeDeviceSlot != null && profile.slot === activeDeviceSlot,
    isDefault: defaultDeviceSlot != null && profile.slot === defaultDeviceSlot,
  }))
}

function getPersistedActiveAppName(): string | null {
  return localStorage.getItem(ACTIVE_APP_PROFILE_KEY)
    ?? localStorage.getItem(LEGACY_ACTIVE_APP_PROFILE_KEY)
}

function persistActiveAppName(name: string) {
  localStorage.setItem(ACTIVE_APP_PROFILE_KEY, name)
  localStorage.setItem(LEGACY_ACTIVE_APP_PROFILE_KEY, name)
}

function clearPersistedActiveAppName() {
  localStorage.removeItem(ACTIVE_APP_PROFILE_KEY)
  localStorage.removeItem(LEGACY_ACTIVE_APP_PROFILE_KEY)
}

export const useProfileStore = create<ProfileStore>((set, get) => ({
  appProfiles: [],
  deviceProfiles: [],
  runtimeSource: "device",
  activeAppProfileName: null,
  activeDeviceSlot: null,
  defaultDeviceSlot: null,
  profileUsedMask: 0,
  ramOnlyActive: false,

  profiles: [],
  selectedProfile: null,

  setRuntimeSource: (runtimeSource) => set({ runtimeSource }),

  setRuntimeDeviceState: (next) => {
    set((state) => {
      const activeDeviceSlot = next.activeDeviceSlot ?? state.activeDeviceSlot
      const defaultDeviceSlot = Object.prototype.hasOwnProperty.call(next, "defaultDeviceSlot")
        ? next.defaultDeviceSlot ?? null
        : state.defaultDeviceSlot
      const activeSlot = Object.prototype.hasOwnProperty.call(next, "activeDeviceSlot")
        ? next.activeDeviceSlot ?? null
        : activeDeviceSlot
      const profileUsedMask = next.profileUsedMask ?? state.profileUsedMask
      const ramOnlyActive = Object.prototype.hasOwnProperty.call(next, "ramOnlyActive")
        ? Boolean(next.ramOnlyActive)
        : state.ramOnlyActive

      const baseProfiles = next.deviceProfiles
        ? next.deviceProfiles.map((profile) => ({
            ...(readDeviceProfileMirror(profile.slot) ?? {}),
            source: "device" as const,
            slot: profile.slot,
            name: profile.name,
            used: profile.used,
            isActive: false,
            isDefault: false,
          }))
        : state.deviceProfiles

      return {
        activeDeviceSlot: activeSlot,
        defaultDeviceSlot,
        profileUsedMask,
        ramOnlyActive,
        deviceProfiles: applyDeviceProfileFlags(baseProfiles, activeSlot, defaultDeviceSlot),
      }
    })
  },

  setDeviceProfiles: (profiles) => {
    set((state) => {
      const nextProfiles = profiles.map((profile) => ({
        ...(readDeviceProfileMirror(profile.slot) ?? {}),
        source: "device" as const,
        slot: profile.slot,
        name: profile.name,
        used: profile.used,
        isActive: false,
        isDefault: false,
      }))
      return {
        deviceProfiles: applyDeviceProfileFlags(
          nextProfiles,
          state.activeDeviceSlot,
          state.defaultDeviceSlot,
        ),
      }
    })
  },

  setActiveDeviceSlot: (activeDeviceSlot) => {
    set((state) => ({
      runtimeSource: "device",
      activeDeviceSlot,
      deviceProfiles: applyDeviceProfileFlags(
        state.deviceProfiles,
        activeDeviceSlot,
        state.defaultDeviceSlot,
      ),
    }))
  },

  setDefaultDeviceSlot: (defaultDeviceSlot) => {
    set((state) => ({
      defaultDeviceSlot,
      deviceProfiles: applyDeviceProfileFlags(
        state.deviceProfiles,
        state.activeDeviceSlot,
        defaultDeviceSlot,
      ),
    }))
  },

  setRamOnlyActive: (ramOnlyActive) => {
    set({ ramOnlyActive })
  },

  upsertDeviceProfileMirror: (slot, name, used, firmwareSnapshot) => {
    writeDeviceProfileMirror(slot, name, used, firmwareSnapshot)
    set((state) => ({
      deviceProfiles: applyDeviceProfileFlags(
        state.deviceProfiles.map((profile) => {
          if (profile.slot !== slot) {
            return profile
          }
          const mirror = readDeviceProfileMirror(slot)
          return {
            ...profile,
            name,
            used,
            ...(mirror?.mirroredAt ? { mirroredAt: mirror.mirroredAt } : {}),
            ...(mirror?.firmwareSnapshot ? { firmwareSnapshot: mirror.firmwareSnapshot } : {}),
          }
        }),
        state.activeDeviceSlot,
        state.defaultDeviceSlot,
      ),
    }))
  },

  removeDeviceProfileMirror: (slot) => {
    localStorage.removeItem(DEVICE_STORAGE_PREFIX + slot)
    set((state) => ({
      deviceProfiles: state.deviceProfiles.map((profile) => {
        if (profile.slot !== slot) {
          return profile
        }
        return {
          source: "device",
          slot: profile.slot,
          name: profile.name,
          used: profile.used,
          isActive: profile.isActive,
          isDefault: profile.isDefault,
        }
      }),
    }))
  },

  getNumberOfProfiles: () => get().appProfiles.length,

  init: () => {
    get().refresh()
  },

  refresh: () => {
    const appProfiles = loadAppProfilesFromStorage()

    set((state) => {
      const preferredName =
        state.activeAppProfileName
        ?? getPersistedActiveAppName()

      const selectedApp = preferredName
        ? appProfiles.find((profile) => profile.name === preferredName) ?? null
        : null
      const profiles = appProfiles.map(toLegacyProfile)
      const selectedProfile = selectedApp ? toLegacyProfile(selectedApp) : null

      return {
        appProfiles,
        profiles,
        activeAppProfileName: selectedProfile?.name ?? null,
        selectedProfile,
      }
    })

    if (get().activeAppProfileName == null) {
      clearPersistedActiveAppName()
    }
  },

  save: (name, options) => {
    const shouldActivate = options?.activate ?? false
    let state = useKeyboardStore.getState()

    if (!get().appProfiles.find((profile) => profile.name === name)) {
      state = { ...state, layout: cloneDefaultLayout() }
    }

    localStorage.setItem(
      APP_STORAGE_PREFIX + name,
      JSON.stringify(toStoredProfileData(state, { firmwareSnapshot: options?.firmwareSnapshot })),
    )
    get().refresh()
    if (shouldActivate) {
      get().selectProfile(name)
    }
  },

  remove: (name) => {
    localStorage.removeItem(APP_STORAGE_PREFIX + name)
    const wasActive = get().activeAppProfileName === name

    get().refresh()

    if (wasActive) {
      set({
        activeAppProfileName: null,
        selectedProfile: null,
        runtimeSource: "device",
      })
      clearPersistedActiveAppName()
    }
  },

  rename: (oldName, newName) => {
    if (!newName.trim() || oldName === newName) return
    if (get().appProfiles.some((profile) => profile.name === newName)) return

    const raw = localStorage.getItem(APP_STORAGE_PREFIX + oldName)
    if (!raw) return

    localStorage.setItem(APP_STORAGE_PREFIX + newName, raw)
    localStorage.removeItem(APP_STORAGE_PREFIX + oldName)

    const wasActive = get().activeAppProfileName === oldName
    if (get().activeAppProfileName === oldName) {
      persistActiveAppName(newName)
    }

    get().refresh()

    if (wasActive) {
      get().selectProfile(newName)
    }
  },

  duplicate: (from, to) => {
    const raw = localStorage.getItem(APP_STORAGE_PREFIX + from)
    if (!raw) return

    localStorage.setItem(APP_STORAGE_PREFIX + to, raw)
    get().refresh()
  },

  getAppProfileByName: (name) => get().appProfiles.find((profile) => profile.name === name) ?? null,

  upsertAppProfileData: (name, data, options) => {
    localStorage.setItem(
      APP_STORAGE_PREFIX + name,
      JSON.stringify(toStoredProfileData(data, { firmwareSnapshot: options?.firmwareSnapshot })),
    )
    get().refresh()

    if (options?.activate) {
      get().selectProfile(name)
    }
  },

  selectProfile: (name) => {
    const profile = get().appProfiles.find((item) => item.name === name)
    if (!profile) return

    const selectedProfile = toLegacyProfile(profile)

    set({
      runtimeSource: "app",
      activeAppProfileName: name,
      selectedProfile,
    })

    useKeyboardStore.setState({
      layout: normalizeKeyboardLayout(profile.data.layout),
      currentLayer: profile.data.currentLayer ?? 0,
    })

    persistActiveAppName(name)
  },

  updateSelectedProfile: (data) => {
    const { selectedProfile } = get()
    if (!selectedProfile) return

    localStorage.setItem(
      APP_STORAGE_PREFIX + selectedProfile.name,
      JSON.stringify(
        toStoredProfileData(data, {
          firmwareSnapshot: selectedProfile.data.firmwareSnapshot,
        }),
      ),
    )
    get().refresh()
  },
}))
