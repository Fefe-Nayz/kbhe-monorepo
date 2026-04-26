import { queryClient } from "@/lib/query/queryClient"
import { queryKeys } from "@/lib/query/keys"
import { kbheDevice } from "./device"
import { DeviceSessionManager, useDeviceSession } from "./session"
import { useProfileStore } from "@/stores/profileStore"
import { applyFirmwareProfileSnapshot, isFirmwareProfileSnapshot } from "./profile-sync"

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

export async function activateDeviceRuntimeProfile(slot: number): Promise<boolean> {
  if (!isRuntimeConnected()) {
    return false
  }

  const { ramOnlyMode } = useDeviceSession.getState()
  if (ramOnlyMode) {
    const exited = await kbheDevice.exitRamOnlyMode()
    if (!exited) {
      return false
    }
  }

  const result = await kbheDevice.setActiveProfile(slot)
  if (!result) {
    return false
  }

  useProfileStore.getState().setRuntimeSource("device")
  useProfileStore.getState().setActiveDeviceSlot(result.profile_index)
  useProfileStore.getState().setRamOnlyActive(false)

  await DeviceSessionManager.refreshRuntimeProfileState()
  hydrateProfileStoreFromSession()
  invalidateRuntimeProfileQueries()
  return true
}

export async function activateTemporaryAppProfile(profileName: string): Promise<boolean> {
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
  const applied = await applyFirmwareProfileSnapshot(snapshot, activeProfileIndex)
  if (!applied) {
    await kbheDevice.exitRamOnlyMode()
    return false
  }

  profileStore.selectProfile(profileName)
  profileStore.setRuntimeSource("app")
  profileStore.setRamOnlyActive(true)
  await DeviceSessionManager.refreshRuntimeProfileState()
  hydrateProfileStoreFromSession()
  invalidateRuntimeProfileQueries()
  return true
}

export async function setDefaultDeviceRuntimeProfile(slot: number | null): Promise<boolean> {
  if (!isRuntimeConnected()) {
    return false
  }

  const target = slot == null ? 0xff : slot
  const result = await kbheDevice.setDefaultProfile(target)
  if (!result) {
    return false
  }

  useProfileStore.getState().setDefaultDeviceSlot(result.profile_index === 0xff ? null : result.profile_index)
  await DeviceSessionManager.refreshRuntimeProfileState()
  hydrateProfileStoreFromSession()
  invalidateRuntimeProfileQueries()
  return true
}

export function hydrateProfileStoreFromSession() {
  const session = useDeviceSession.getState()
  const profileStore = useProfileStore.getState()
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
