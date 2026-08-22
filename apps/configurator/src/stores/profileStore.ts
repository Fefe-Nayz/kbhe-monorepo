import { create } from "zustand"
import { useKeyboardStore, type KeyboardState } from "./keyboard-store"
import { cloneDefaultLayout, normalizeKeyboardLayout } from "@/constants/defaultLayout"
import {
  isFirmwareProfileSnapshot,
  type FirmwareProfileSnapshot,
} from "@/lib/kbhe/profile-sync"
import {
  getProfileStorageItem,
  getProfileStorageKeys,
  initializeProfilePersistence,
  removeProfileStorageItem,
  setProfileStorageItem,
  subscribeProfilePersistenceErrors,
} from "@/lib/kbhe/profile-persistence"

const APP_STORAGE_PREFIX = "keyboard-profile:"
const DEVICE_STORAGE_PREFIX = "keyboard-device-profile:"
const ACTIVE_APP_PROFILE_KEY = "keyboard-active-app-profile"
const LEGACY_ACTIVE_APP_PROFILE_KEY = "keyboard-active-profile"
let profileStoreInitialized = false

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
  deviceId: string
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
  deviceId: string | null
  persistenceError: string | null

  // Legacy compatibility for existing UI surface
  profiles: KeyboardProfile[]
  selectedProfile: KeyboardProfile | null

  // Runtime controls
  setRuntimeSource: (source: RuntimeSource) => void
  setDeviceId: (deviceId: string | null) => void
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
  const snapshotCandidate = (data as Partial<KeyboardProfileData>).firmwareSnapshot
  if (snapshotCandidate !== undefined && !isFirmwareProfileSnapshot(snapshotCandidate)) {
    throw new Error("Invalid firmware profile document")
  }
  if (
    options?.firmwareSnapshot !== undefined
    && options.firmwareSnapshot !== null
    && !isFirmwareProfileSnapshot(options.firmwareSnapshot)
  ) {
    throw new Error("Invalid firmware profile document")
  }
  const directSnapshot = snapshotCandidate
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

  for (const key of getProfileStorageKeys()) {
    if (!key.startsWith(APP_STORAGE_PREFIX)) continue

    const raw = getProfileStorageItem(key)
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

function deviceMirrorStorageKey(deviceId: string, slot: number): string {
  return `${DEVICE_STORAGE_PREFIX}${encodeURIComponent(deviceId)}:${slot}`
}

function readDeviceProfileMirror(deviceId: string | null, slot: number): DeviceProfileMirror | null {
  if (!deviceId) {
    return null
  }

  const raw = getProfileStorageItem(deviceMirrorStorageKey(deviceId, slot))
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
      deviceId,
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
  deviceId: string | null,
  slot: number,
  name: string,
  used: boolean,
  firmwareSnapshot?: FirmwareProfileSnapshot | null,
) {
  if (!deviceId) {
    return
  }

  const existing = readDeviceProfileMirror(deviceId, slot)
  const snapshot = firmwareSnapshot === undefined
    ? existing?.firmwareSnapshot
    : firmwareSnapshot ?? undefined

  const record: DeviceProfileMirror = {
    source: "device",
    deviceId,
    slot,
    name,
    used,
    mirroredAt: Date.now(),
    ...(snapshot ? { firmwareSnapshot: snapshot } : {}),
  }

  setProfileStorageItem(deviceMirrorStorageKey(deviceId, slot), JSON.stringify(record))
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
  return getProfileStorageItem(ACTIVE_APP_PROFILE_KEY)
    ?? getProfileStorageItem(LEGACY_ACTIVE_APP_PROFILE_KEY)
}

function persistActiveAppName(name: string) {
  setProfileStorageItem(ACTIVE_APP_PROFILE_KEY, name)
  setProfileStorageItem(LEGACY_ACTIVE_APP_PROFILE_KEY, name)
}

function clearPersistedActiveAppName() {
  removeProfileStorageItem(ACTIVE_APP_PROFILE_KEY)
  removeProfileStorageItem(LEGACY_ACTIVE_APP_PROFILE_KEY)
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
  deviceId: null,
  persistenceError: null,

  profiles: [],
  selectedProfile: null,

  setRuntimeSource: (runtimeSource) => set({ runtimeSource }),

  setDeviceId: (deviceId) => set((state) => ({
    deviceId,
    deviceProfiles: state.deviceProfiles.map((profile) => ({
      ...(readDeviceProfileMirror(deviceId, profile.slot) ?? {}),
      ...profile,
      isActive: profile.isActive,
      isDefault: profile.isDefault,
    })),
  })),

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
            ...(readDeviceProfileMirror(state.deviceId, profile.slot) ?? {}),
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
        ...(readDeviceProfileMirror(state.deviceId, profile.slot) ?? {}),
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
    const deviceId = get().deviceId
    writeDeviceProfileMirror(deviceId, slot, name, used, firmwareSnapshot)
    set((state) => ({
      deviceProfiles: applyDeviceProfileFlags(
        state.deviceProfiles.map((profile) => {
          if (profile.slot !== slot) {
            return profile
          }
          const mirror = readDeviceProfileMirror(deviceId, slot)
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
    const deviceId = get().deviceId
    if (deviceId) {
      removeProfileStorageItem(deviceMirrorStorageKey(deviceId, slot))
    }
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
    if (profileStoreInitialized) return
    profileStoreInitialized = true
    subscribeProfilePersistenceErrors((persistenceError) => set({ persistenceError }))
    void initializeProfilePersistence().then(() => get().refresh())
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

    setProfileStorageItem(
      APP_STORAGE_PREFIX + name,
      JSON.stringify(toStoredProfileData(state, { firmwareSnapshot: options?.firmwareSnapshot })),
    )
    get().refresh()
    if (shouldActivate) {
      get().selectProfile(name)
    }
  },

  remove: (name) => {
    removeProfileStorageItem(APP_STORAGE_PREFIX + name)
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

    const raw = getProfileStorageItem(APP_STORAGE_PREFIX + oldName)
    if (!raw) return

    setProfileStorageItem(APP_STORAGE_PREFIX + newName, raw)
    removeProfileStorageItem(APP_STORAGE_PREFIX + oldName)

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
    const raw = getProfileStorageItem(APP_STORAGE_PREFIX + from)
    if (!raw) return

    setProfileStorageItem(APP_STORAGE_PREFIX + to, raw)
    get().refresh()
  },

  getAppProfileByName: (name) => get().appProfiles.find((profile) => profile.name === name) ?? null,

  upsertAppProfileData: (name, data, options) => {
    setProfileStorageItem(
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

    setProfileStorageItem(
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
