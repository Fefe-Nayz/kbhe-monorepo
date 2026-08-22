import { queryClient } from "@/lib/query/queryClient"
import { queryKeys } from "@/lib/query/keys"
import { kbheDevice } from "./device"
import { DeviceSessionManager, useDeviceSession } from "./session"
import { useProfileStore } from "@/stores/profileStore"
import { kbheDeviceStorageId } from "./transport"
import { runProfileOperation, type ProfileOperationToken } from "./profile-operation-lock"
import {
  applyFirmwareProfileSnapshot,
  captureFirmwareProfileSnapshot,
  isFirmwareProfileSnapshot,
} from "./profile-sync"

function invalidateRuntimeProfileQueries() {
  void queryClient.invalidateQueries({ queryKey: queryKeys.profile.active() })
  void queryClient.invalidateQueries({ queryKey: queryKeys.profile.default() })
  void queryClient.invalidateQueries({ queryKey: queryKeys.profile.names() })
  void queryClient.invalidateQueries({ queryKey: queryKeys.profile.usedMask() })
  void queryClient.invalidateQueries({ queryKey: queryKeys.profile.ramOnly() })
}

function isRuntimeConnected() {
  return useDeviceSession.getState().status === "connected"
}

async function resyncRuntimeProfileState(): Promise<void> {
  await DeviceSessionManager.refreshRuntimeProfileState()
  hydrateProfileStoreFromSession()
  invalidateRuntimeProfileQueries()
}

async function ensurePersistentDeviceRuntimeUnlocked(): Promise<boolean> {
  if (!isRuntimeConnected()) {
    return false
  }

  const ramOnlyMode =
    useDeviceSession.getState().ramOnlyMode ?? await kbheDevice.getRamOnlyMode()
  if (!ramOnlyMode) {
    return true
  }

  const exited = await kbheDevice.exitRamOnlyMode()
  if (!exited) {
    return false
  }

  useDeviceSession.getState()._setRuntimeProfileState({ ramOnlyMode: false })
  useProfileStore.getState().setRuntimeSource("device")
  useProfileStore.getState().setRamOnlyActive(false)
  await resyncRuntimeProfileState()
  return true
}

async function activateDeviceRuntimeProfileUnlocked(slot: number): Promise<boolean> {
  if (!isRuntimeConnected()) {
    return false
  }

  const { ramOnlyMode } = useDeviceSession.getState()
  if (ramOnlyMode) {
    const exited = await kbheDevice.exitRamOnlyMode()
    if (!exited) {
      return false
    }
    useDeviceSession.getState()._setRuntimeProfileState({ ramOnlyMode: false })
    useProfileStore.getState().setRuntimeSource("device")
    useProfileStore.getState().setRamOnlyActive(false)
  }

  const result = await kbheDevice.setActiveProfile(slot)
  if (!result) {
    await resyncRuntimeProfileState()
    return false
  }
  const saved = await kbheDevice.saveSettings()
  if (!saved) {
    // SET_ACTIVE_PROFILE already changed RAM. Reload the last durable state so
    // a rejected flash save cannot leave the UI and keyboard on different slots.
    const reloaded = await kbheDevice.exitRamOnlyMode()
    if (reloaded) {
      useDeviceSession.getState()._setRuntimeProfileState({ ramOnlyMode: false })
    } else {
      useDeviceSession.getState()._setRuntimeProfileState({
        activeProfileIndex: result.profile_index,
        profileUsedMask: result.profile_used_mask,
        ramOnlyMode: false,
      })
    }
    useProfileStore.getState().setRuntimeSource("device")
    useProfileStore.getState().setRamOnlyActive(false)
    await resyncRuntimeProfileState()
    return false
  }

  useDeviceSession.getState()._setRuntimeProfileState({
    activeProfileIndex: result.profile_index,
    profileUsedMask: result.profile_used_mask,
    ramOnlyMode: false,
  })
  useProfileStore.getState().setRuntimeSource("device")
  useProfileStore.getState().setActiveDeviceSlot(result.profile_index)
  useProfileStore.getState().setRamOnlyActive(false)

  await resyncRuntimeProfileState()
  return true
}

async function activateTemporaryAppProfileUnlocked(
  profileName: string,
  operationToken: ProfileOperationToken,
): Promise<boolean> {
  const profileStore = useProfileStore.getState()
  const appProfile = profileStore.getAppProfileByName(profileName)
  if (!appProfile) {
    return false
  }

  const snapshot = appProfile.data.firmwareSnapshot

  if (!isRuntimeConnected()) {
    profileStore.selectProfile(profileName)
    profileStore.setRuntimeSource("app")
    return true
  }

  if (!isFirmwareProfileSnapshot(snapshot)) {
    return false
  }

  const entered = await kbheDevice.enterRamOnlyMode()
  if (!entered) {
    return false
  }

  const activeProfileIndex = useDeviceSession.getState().activeProfileIndex ?? 0
  const applied = await applyFirmwareProfileSnapshot(snapshot, activeProfileIndex, { operationToken })
  if (!applied) {
    const exited = await kbheDevice.exitRamOnlyMode()
    if (exited) {
      useDeviceSession.getState()._setRuntimeProfileState({ ramOnlyMode: false })
      profileStore.setRuntimeSource("device")
      profileStore.setRamOnlyActive(false)
    }
    await resyncRuntimeProfileState()
    return false
  }

  useDeviceSession.getState()._setRuntimeProfileState({
    activeProfileIndex,
    ramOnlyMode: true,
  })
  profileStore.selectProfile(profileName)
  profileStore.setRuntimeSource("app")
  profileStore.setRamOnlyActive(true)
  await resyncRuntimeProfileState()
  return true
}

async function setDefaultDeviceRuntimeProfileUnlocked(slot: number | null): Promise<boolean> {
  if (!isRuntimeConnected()) {
    return false
  }

  const persistent = await ensurePersistentDeviceRuntimeUnlocked()
  if (!persistent) {
    return false
  }

  const target = slot == null ? 0xff : slot
  const result = await kbheDevice.setDefaultProfile(target)
  if (!result) {
    return false
  }
  const saved = await kbheDevice.saveSettings()
  if (!saved) {
    await kbheDevice.exitRamOnlyMode()
    await resyncRuntimeProfileState()
    return false
  }

  useDeviceSession.getState()._setRuntimeProfileState({
    defaultProfileIndex: result.profile_index === 0xff ? null : result.profile_index,
  })
  useProfileStore.getState().setDefaultDeviceSlot(result.profile_index === 0xff ? null : result.profile_index)
  await resyncRuntimeProfileState()
  return true
}

export function hydrateProfileStoreFromSession() {
  const session = useDeviceSession.getState()
  const profileStore = useProfileStore.getState()
  profileStore.setDeviceId(session.deviceInfo ? kbheDeviceStorageId(session.deviceInfo) : null)
  const deviceProfiles = session.profileNames.map((name, slot) => ({
    slot,
    name,
    used: Boolean(session.profileUsedMask & (1 << slot)),
  }))

  profileStore.setRuntimeDeviceState({
    activeDeviceSlot: session.activeProfileIndex,
    defaultDeviceSlot: session.defaultProfileIndex,
    ramOnlyActive: Boolean(session.ramOnlyMode),
    profileUsedMask: session.profileUsedMask,
    deviceProfiles,
  })

  const hasActiveAppProfile = Boolean(profileStore.activeAppProfileName)
  profileStore.setRuntimeSource(session.ramOnlyMode && hasActiveAppProfile ? "app" : "device")
}

async function syncActiveDeviceProfileMirrorFromKeyboardUnlocked(
  operationToken: ProfileOperationToken,
): Promise<void> {
  const session = useDeviceSession.getState()
  if (
    session.status !== "connected" ||
    session.ramOnlyMode ||
    session.activeProfileIndex == null ||
    (session.profileUsedMask & (1 << session.activeProfileIndex)) === 0
  ) {
    return
  }

  const snapshot = await captureFirmwareProfileSnapshot(session.activeProfileIndex, operationToken)
  if (!snapshot) {
    return
  }

  const name = session.profileNames[session.activeProfileIndex]
    ?? `Slot ${session.activeProfileIndex + 1}`
  useProfileStore.getState().upsertDeviceProfileMirror(
    session.activeProfileIndex,
    name,
    true,
    snapshot,
  )
}

export function ensurePersistentDeviceRuntime(): Promise<boolean> {
  return runProfileOperation(() => ensurePersistentDeviceRuntimeUnlocked())
}

export function activateDeviceRuntimeProfile(slot: number): Promise<boolean> {
  return runProfileOperation(() => activateDeviceRuntimeProfileUnlocked(slot))
}

export function activateTemporaryAppProfile(profileName: string): Promise<boolean> {
  return runProfileOperation((operationToken) => (
    activateTemporaryAppProfileUnlocked(profileName, operationToken)
  ))
}

export function setDefaultDeviceRuntimeProfile(slot: number | null): Promise<boolean> {
  return runProfileOperation(() => setDefaultDeviceRuntimeProfileUnlocked(slot))
}

export function syncActiveDeviceProfileMirrorFromKeyboard(): Promise<void> {
  return runProfileOperation((operationToken) => (
    syncActiveDeviceProfileMirrorFromKeyboardUnlocked(operationToken)
  ))
}
