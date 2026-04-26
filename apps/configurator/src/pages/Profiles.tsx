import { useState, useEffect, useRef, useCallback, useMemo } from "react";
import { useQueryClient } from "@tanstack/react-query";
import { useProfileStore } from "@/stores/profileStore";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { DeviceSessionManager, useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import {
  activateDeviceRuntimeProfile,
  activateTemporaryAppProfile,
  ensurePersistentDeviceRuntime,
  hydrateProfileStoreFromSession,
  setDefaultDeviceRuntimeProfile,
  syncActiveDeviceProfileMirrorFromKeyboard,
} from "@/lib/kbhe/profile-runtime";
import {
  applyFirmwareProfileSnapshot,
  captureFirmwareProfileSnapshot,
  isFirmwareProfileSnapshot,
} from "@/lib/kbhe/profile-sync";
import { SETTINGS_PROFILE_COUNT } from "@/lib/kbhe/protocol";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import { SectionCard } from "@/components/shared/SectionCard";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import {
  IconPlus,
  IconTrash,
  IconCopy,
  IconCheck,
  IconDownload,
  IconUpload,
  IconDotsVertical,
  IconRefresh,
  IconPencil,
  IconDeviceFloppy,
  IconRotate,
} from "@tabler/icons-react";
import { toast } from "sonner";
import { PageContent } from "@/components/shared/PageLayout";

type ProfileTargetType = "device" | "app";

type UnifiedProfile =
  | {
      id: string;
      kind: "device";
      name: string;
      slot: number;
      used: boolean;
      isRuntimeActive: boolean;
      isDefault: boolean;
    }
  | {
      id: string;
      kind: "app";
      name: string;
      appId: string;
      isRuntimeActive: boolean;
    };

function countUsedSlots(mask: number): number {
  let count = 0;
  for (let i = 0; i < SETTINGS_PROFILE_COUNT; i += 1) {
    if (mask & (1 << i)) {
      count += 1;
    }
  }
  return count;
}

function firstFreeDeviceSlot(mask: number): number | null {
  for (let i = 0; i < SETTINGS_PROFILE_COUNT; i += 1) {
    if ((mask & (1 << i)) === 0) {
      return i;
    }
  }
  return null;
}

function firstUsedDeviceSlot(mask: number): number | null {
  for (let i = 0; i < SETTINGS_PROFILE_COUNT; i += 1) {
    if ((mask & (1 << i)) !== 0) {
      return i;
    }
  }
  return null;
}

function uniqueName(base: string, existingNames: Set<string>): string {
  const trimmed = base.trim() || "Profile";
  if (!existingNames.has(trimmed)) {
    return trimmed;
  }

  let suffix = 2;
  while (existingNames.has(`${trimmed} ${suffix}`)) {
    suffix += 1;
  }

  return `${trimmed} ${suffix}`;
}

function buildCurrentAppProfilePayload() {
  const keyboard = useKeyboardStore.getState();
  return {
    layout: keyboard.layout,
    mode: keyboard.mode,
    displayedInfo: keyboard.displayedInfo,
    currentLayer: keyboard.currentLayer,
  };
}

export default function Profiles() {
  const queryClient = useQueryClient();

  const appProfiles = useProfileStore((s) => s.appProfiles);
  const runtimeSource = useProfileStore((s) => s.runtimeSource);
  const activeAppProfileName = useProfileStore((s) => s.activeAppProfileName);
  const ramOnlyFromStore = useProfileStore((s) => s.ramOnlyActive);

  const upsertAppProfileData = useProfileStore((s) => s.upsertAppProfileData);
  const getAppProfileByName = useProfileStore((s) => s.getAppProfileByName);
  const remove = useProfileStore((s) => s.remove);
  const rename = useProfileStore((s) => s.rename);
  const duplicate = useProfileStore((s) => s.duplicate);
  const init = useProfileStore((s) => s.init);

  const status = useDeviceSession((s) => s.status);
  const activeProfileIndex = useDeviceSession((s) => s.activeProfileIndex);
  const defaultProfileIndex = useDeviceSession((s) => s.defaultProfileIndex);
  const profileUsedMask = useDeviceSession((s) => s.profileUsedMask);
  const profileNames = useDeviceSession((s) => s.profileNames);
  const ramOnlyMode = useDeviceSession((s) => s.ramOnlyMode);

  const connected = status === "connected";
  const effectiveRamOnly = connected ? Boolean(ramOnlyMode) : ramOnlyFromStore;
  const usedDeviceSlots = useMemo(() => countUsedSlots(profileUsedMask), [profileUsedMask]);
  const freeDeviceSlot = useMemo(() => firstFreeDeviceSlot(profileUsedMask), [profileUsedMask]);

  const [actionPending, setActionPending] = useState(false);

  const [deleteTarget, setDeleteTarget] = useState<UnifiedProfile | null>(null);
  const [renameTarget, setRenameTarget] = useState<UnifiedProfile | null>(null);
  const [renameValue, setRenameValue] = useState("");

  const [duplicateTarget, setDuplicateTarget] = useState<UnifiedProfile | null>(null);
  const [duplicateName, setDuplicateName] = useState("");
  const [duplicateType, setDuplicateType] = useState<ProfileTargetType>("app");

  const [createOpen, setCreateOpen] = useState(false);
  const [newName, setNewName] = useState("");
  const [createType, setCreateType] = useState<ProfileTargetType>("app");

  const importRef = useRef<HTMLInputElement>(null);
  const importTargetRef = useRef<string | null>(null);

  const syncRuntimeState = useCallback(async () => {
    await DeviceSessionManager.refreshRuntimeProfileState();
    hydrateProfileStoreFromSession();
    await syncActiveDeviceProfileMirrorFromKeyboard();
    void queryClient.invalidateQueries({ queryKey: ["profile"] });
    void queryClient.invalidateQueries({ queryKey: ["keymap"] });
  }, [queryClient]);

  const runAction = useCallback(async (task: () => Promise<void>) => {
    if (actionPending) return;

    setActionPending(true);
    try {
      await task();
    } finally {
      setActionPending(false);
    }
  }, [actionPending]);

  const upsertLocalAppProfile = useCallback(async (
    profileName: string,
    source?: UnifiedProfile,
  ): Promise<boolean> => {
    let firmwareSnapshot = null;

    if (source?.kind === "device") {
      if (!connected) {
        return false;
      }

      if (!await ensurePersistentDeviceRuntime()) {
        return false;
      }

      firmwareSnapshot = await captureFirmwareProfileSnapshot(source.slot);
      if (!firmwareSnapshot) {
        return false;
      }
    } else if (connected && activeProfileIndex != null) {
      firmwareSnapshot = await captureFirmwareProfileSnapshot(activeProfileIndex);
      if (!firmwareSnapshot) {
        return false;
      }
    }

    upsertAppProfileData(
      profileName,
      buildCurrentAppProfilePayload(),
      { firmwareSnapshot, activate: false },
    );
    return true;
  }, [activeProfileIndex, connected, upsertAppProfileData]);

  const existingAppNames = useMemo(
    () => new Set(appProfiles.map((profile) => profile.name)),
    [appProfiles],
  );

  const openDuplicateDialog = useCallback((profile: UnifiedProfile, preferredType?: ProfileTargetType) => {
    const nextType = preferredType ?? profile.kind;
    const baseName = `${profile.name} copy`;
    const suggested = nextType === "app"
      ? uniqueName(baseName, existingAppNames)
      : baseName;

    setDuplicateTarget(profile);
    setDuplicateType(nextType);
    setDuplicateName(suggested);
  }, [existingAppNames]);

  useEffect(() => {
    init();
    useKeyboardStore.getState().setSaveEnabled(true);
    return () => { useKeyboardStore.getState().setSaveEnabled(false); };
  }, [init]);

  useEffect(() => {
    if (!connected) return;
    void syncRuntimeState();
  }, [connected, syncRuntimeState]);

  useEffect(() => {
    if (!connected) return;
    hydrateProfileStoreFromSession();
  }, [
    connected,
    activeProfileIndex,
    defaultProfileIndex,
    profileUsedMask,
    profileNames,
    ramOnlyMode,
  ]);

  const deviceProfiles: UnifiedProfile[] = useMemo(() => {
    const names = profileNames.length === SETTINGS_PROFILE_COUNT
      ? profileNames
      : Array.from({ length: SETTINGS_PROFILE_COUNT }, (_, index) => `Slot ${index + 1}`);

    return names
      .map((name, slot) => ({
        id: `device:${slot}`,
        kind: "device" as const,
        name,
        slot,
        used: Boolean(profileUsedMask & (1 << slot)),
        isRuntimeActive: runtimeSource === "device" && activeProfileIndex === slot,
        isDefault: defaultProfileIndex === slot,
      }))
      .filter((profile) => profile.used || profile.isRuntimeActive || profile.isDefault);
  }, [profileNames, profileUsedMask, runtimeSource, activeProfileIndex, defaultProfileIndex]);

  const appProfileItems: UnifiedProfile[] = useMemo(
    () => appProfiles.map((profile) => ({
      id: `app:${profile.id}`,
      kind: "app" as const,
      appId: profile.id,
      name: profile.name,
      isRuntimeActive: runtimeSource === "app" && activeAppProfileName === profile.name,
    })),
    [appProfiles, runtimeSource, activeAppProfileName],
  );

  const profiles = useMemo(() => {
    const merged = [...deviceProfiles, ...appProfileItems];
    merged.sort((a, b) => {
      if (a.isRuntimeActive !== b.isRuntimeActive) {
        return a.isRuntimeActive ? -1 : 1;
      }
      if (a.kind !== b.kind) {
        return a.kind === "device" ? -1 : 1;
      }
      return a.name.localeCompare(b.name, undefined, { sensitivity: "base" });
    });
    return merged;
  }, [deviceProfiles, appProfileItems]);

  const handleCreate = () => {
    const name = newName.trim();
    if (!name) return;

    void runAction(async () => {
      if (createType === "app") {
        if (existingAppNames.has(name)) {
          toast.error("An app profile with this name already exists");
          return;
        }

        const created = await upsertLocalAppProfile(name);
        if (!created) {
          toast.error("Failed to capture current firmware state for this app profile");
          return;
        }

        toast.success(`App profile "${name}" created`);
      } else {
        if (!connected) {
          toast.error("Connect the keyboard to create a device profile");
          return;
        }
        if (freeDeviceSlot == null) {
          toast.error("No free device slot available");
          return;
        }

        if (!await ensurePersistentDeviceRuntime()) {
          toast.error("Failed to leave temporary mode before creating a device profile");
          return;
        }

        const created = await kbheDevice.createProfile(name);
        if (!created) {
          toast.error("Failed to create device profile");
          return;
        }
        const snapshot = await captureFirmwareProfileSnapshot(created.profile_index);
        useProfileStore.getState().upsertDeviceProfileMirror(
          created.profile_index,
          name,
          true,
          snapshot ?? undefined,
        );
        if (!await kbheDevice.saveSettings()) {
          toast.error("Created, but failed to persist device profile");
          return;
        }

        await syncRuntimeState();
        toast.success(`Device profile "${name}" created`);
      }

      setNewName("");
      setCreateOpen(false);
    });
  };

  const handleExport = (profileName: string) => {
    const profile = appProfiles.find((p) => p.name === profileName);
    if (!profile) return;
    const blob = new Blob([JSON.stringify(profile, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${profileName}.json`;
    a.click();
    URL.revokeObjectURL(url);
    toast.success(`Exported "${profileName}"`);
  };

  const handleImportClick = (profileName: string) => {
    importTargetRef.current = profileName;
    importRef.current?.click();
  };

  const handleImportFile = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    const target = importTargetRef.current;
    if (!file || !target) return;

    const reader = new FileReader();
    reader.onload = () => {
      try {
        const json = JSON.parse(reader.result as string);
        const payload = json && typeof json === "object" && "data" in json ? json.data : json;
        if (!payload || typeof payload !== "object") {
          throw new Error("Invalid payload");
        }

        upsertAppProfileData(target, payload as Record<string, unknown>, { activate: false });
        toast.success(`Imported into "${target}"`);
      } catch {
        toast.error("Invalid JSON file");
      }
    };
    reader.readAsText(file);
    e.target.value = "";
  }, [upsertAppProfileData]);

  const handleActivateProfile = (profile: UnifiedProfile) => {
    void runAction(async () => {
      if (profile.kind === "device") {
        const ok = await activateDeviceRuntimeProfile(profile.slot);
        if (!ok) {
          toast.error("Failed to activate device profile");
          return;
        }
        toast.success(`Now using device profile "${profile.name}"`);
        return;
      }

      if (connected) {
        const source = getAppProfileByName(profile.name);
        if (!source || !isFirmwareProfileSnapshot(source.data.firmwareSnapshot)) {
          toast.error("This app profile cannot be applied: no firmware snapshot is available");
          return;
        }
      }

      const ok = await activateTemporaryAppProfile(profile.name);
      if (!ok) {
        toast.error("Failed to apply app profile in temporary mode");
        return;
      }

      toast.success(`Temporary RAM session started from "${profile.name}"`);
    });
  };

  const handleRename = () => {
    if (!renameTarget) return;
    const nextName = renameValue.trim();
    if (!nextName) return;

    void runAction(async () => {
      if (renameTarget.kind === "app") {
        if (existingAppNames.has(nextName) && nextName !== renameTarget.name) {
          toast.error("An app profile with this name already exists");
          return;
        }
        rename(renameTarget.name, nextName);
        toast.success(`Renamed to "${nextName}"`);
      } else {
        if (!connected) {
          toast.error("Connect the keyboard to rename this profile");
          return;
        }

        if (!await ensurePersistentDeviceRuntime()) {
          toast.error("Failed to leave temporary mode before renaming this profile");
          return;
        }

        const renamed = await kbheDevice.setProfileName(renameTarget.slot, nextName);
        if (!renamed) {
          toast.error("Failed to rename device profile");
          return;
        }
        useProfileStore.getState().upsertDeviceProfileMirror(renameTarget.slot, nextName, true);
        if (!await kbheDevice.saveSettings()) {
          toast.error("Renamed, but failed to persist device profile");
          return;
        }

        await syncRuntimeState();
        toast.success(`Renamed to "${nextName}"`);
      }

      setRenameTarget(null);
    });
  };

  const handleDelete = () => {
    if (!deleteTarget) return;

    void runAction(async () => {
      if (deleteTarget.kind === "app") {
        if (deleteTarget.isRuntimeActive && connected) {
          const fallbackSlot = defaultProfileIndex
            ?? activeProfileIndex
            ?? firstUsedDeviceSlot(profileUsedMask)
            ?? 0;
          const switched = await activateDeviceRuntimeProfile(fallbackSlot);
          if (!switched) {
            toast.error("Failed to switch back to a device profile before deletion");
            return;
          }
        }

        remove(deleteTarget.name);
        toast.success(`Deleted "${deleteTarget.name}"`);
      } else {
        if (!connected) {
          toast.error("Connect the keyboard to delete this profile");
          return;
        }

        if (usedDeviceSlots <= 1 && deleteTarget.used) {
          toast.error("Cannot delete the last remaining device profile");
          return;
        }

        if (!await ensurePersistentDeviceRuntime()) {
          toast.error("Failed to leave temporary mode before deleting this profile");
          return;
        }

        const deleted = await kbheDevice.deleteProfile(deleteTarget.slot);
        if (!deleted) {
          toast.error("Failed to delete device profile");
          return;
        }
        useProfileStore.getState().removeDeviceProfileMirror(deleteTarget.slot);
        if (!await kbheDevice.saveSettings()) {
          toast.error("Deleted, but failed to persist device profile changes");
          return;
        }

        await syncRuntimeState();
        toast.success(`Deleted "${deleteTarget.name}"`);
      }

      setDeleteTarget(null);
    });
  };

  const handleDuplicate = () => {
    if (!duplicateTarget) return;
    const nextName = duplicateName.trim();
    if (!nextName) return;

    void runAction(async () => {
      if (duplicateType === "app") {
        if (existingAppNames.has(nextName)) {
          toast.error("An app profile with this name already exists");
          return;
        }

        if (duplicateTarget.kind === "app") {
          duplicate(duplicateTarget.name, nextName);
        } else {
          const created = await upsertLocalAppProfile(nextName, duplicateTarget);
          if (!created) {
            toast.error("Failed to capture the device profile into an app profile");
            return;
          }
        }

        toast.success(`Created app profile "${nextName}"`);
      } else {
        if (!connected) {
          toast.error("Connect the keyboard to create a device profile");
          return;
        }

        const targetSlot = firstFreeDeviceSlot(profileUsedMask);
        if (targetSlot == null) {
          toast.error("No free device slot available");
          return;
        }

        if (!await ensurePersistentDeviceRuntime()) {
          toast.error("Failed to leave temporary mode before creating a device profile");
          return;
        }

        if (duplicateTarget.kind === "device") {
          const copied = await kbheDevice.copyProfileSlot(duplicateTarget.slot, targetSlot);
          if (!copied) {
            toast.error("Failed to duplicate device profile");
            return;
          }
          const renamed = await kbheDevice.setProfileName(targetSlot, nextName);
          if (!renamed) {
            toast.error("Duplicated, but failed to set profile name");
            return;
          }
          const snapshot = await captureFirmwareProfileSnapshot(targetSlot);
          useProfileStore.getState().upsertDeviceProfileMirror(
            targetSlot,
            nextName,
            true,
            snapshot ?? undefined,
          );
          if (!await kbheDevice.saveSettings()) {
            toast.error("Duplicated, but failed to persist device profile");
            return;
          }
        } else {
          const sourceApp = getAppProfileByName(duplicateTarget.name);
          if (!sourceApp) {
            toast.error("Source app profile not found");
            return;
          }

          const snapshot = sourceApp.data.firmwareSnapshot;
          if (!isFirmwareProfileSnapshot(snapshot)) {
            toast.error("This app profile has no firmware snapshot to apply to the device");
            return;
          }

          const created = await kbheDevice.createProfile(nextName);
          if (!created) {
            toast.error("Failed to create device profile from app profile");
            return;
          }

          const applied = await applyFirmwareProfileSnapshot(snapshot, created.profile_index, {
            persistToFlash: true,
          });
          if (!applied) {
            // Best effort rollback to avoid leaving a partially-applied profile slot around.
            await kbheDevice.deleteProfile(created.profile_index);
            await kbheDevice.saveSettings();
            toast.error("Failed to apply app profile data to the new device slot");
            return;
          }
          useProfileStore.getState().upsertDeviceProfileMirror(created.profile_index, nextName, true, snapshot);
        }

        await syncRuntimeState();
        toast.success(`Created device profile "${nextName}"`);
      }

      setDuplicateTarget(null);
    });
  };

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent>
        <SectionCard
          title="Profiles"
          description="Device and app profiles are managed together. Device profiles occupy keyboard slots; app profiles are unlimited and can be applied in temporary RAM-only mode."
        >
          <div className="mb-4 flex flex-wrap items-center gap-2">
            <Badge variant="secondary">Device Slots: {usedDeviceSlots}/{SETTINGS_PROFILE_COUNT}</Badge>
            <Badge variant={effectiveRamOnly ? "default" : "outline"}>
              Runtime: {runtimeSource === "app" && effectiveRamOnly ? "Temporary App (RAM only)" : runtimeSource === "device" ? "Device Profile" : "App Profile"}
            </Badge>
            {!connected && (
              <Badge variant="outline">Keyboard disconnected</Badge>
            )}
          </div>

          {!connected && profiles.length === 0 ? (
            <div className="flex flex-col gap-2">
              <Skeleton className="h-22 w-full" />
              <Skeleton className="h-22 w-full" />
            </div>
          ) : (
            <div className="grid grid-cols-1 xl:grid-cols-2 gap-4">
              {profiles.map((profile) => {
                const canDeleteDevice = profile.kind === "device" ? usedDeviceSlots > 1 || !profile.used : true;

                return (
                  <div
                    key={profile.id}
                    className="rounded-lg border bg-card p-4 shadow-sm flex flex-col gap-3 transition-colors hover:border-primary/30"
                  >
                    <div className="flex items-start justify-between">
                      <div className="flex flex-col gap-1 min-w-0">
                        <span className="text-sm font-medium truncate">{profile.name}</span>
                        <div className="flex flex-wrap items-center gap-1.5">
                          <Badge variant="outline" className="text-[10px]">
                            {profile.kind === "device" ? `Device Slot ${profile.slot + 1}` : "App"}
                          </Badge>
                          {profile.isRuntimeActive && (
                            <Badge className="gap-1 text-[10px]">
                              <IconCheck className="size-3" /> Running
                            </Badge>
                          )}
                          {profile.kind === "device" && profile.isDefault && (
                            <Badge variant="secondary" className="text-[10px]">Boot Default</Badge>
                          )}
                          {profile.kind === "app" && profile.isRuntimeActive && effectiveRamOnly && (
                            <Badge variant="secondary" className="text-[10px]">Temporary RAM</Badge>
                          )}
                        </div>
                      </div>

                      <DropdownMenu>
                        <DropdownMenuTrigger render={
                          <Button variant="ghost" size="icon" className="size-7">
                            <IconDotsVertical className="size-4" />
                            <span className="sr-only">Profile actions</span>
                          </Button>
                        } />
                        <DropdownMenuContent align="end">
                          {!profile.isRuntimeActive && (
                            <DropdownMenuItem
                              disabled={actionPending || (profile.kind === "device" && !connected)}
                              onClick={() => handleActivateProfile(profile)}
                            >
                              <IconCheck className="size-4" />
                              {profile.kind === "device" ? "Use on Keyboard" : "Apply Temporary"}
                            </DropdownMenuItem>
                          )}

                          {profile.kind === "device" && (
                            <>
                              <DropdownMenuItem
                                disabled={actionPending || !connected}
                                onClick={() => {
                                  void runAction(async () => {
                                    const ok = await setDefaultDeviceRuntimeProfile(profile.isDefault ? null : profile.slot);
                                    if (!ok) {
                                      toast.error("Failed to update default boot profile");
                                      return;
                                    }
                                    toast.success(profile.isDefault ? "Boot default cleared" : "Boot default updated");
                                  });
                                }}
                              >
                                <IconDeviceFloppy className="size-4" />
                                {profile.isDefault ? "Clear Boot Default" : "Set as Boot Default"}
                              </DropdownMenuItem>
                              <DropdownMenuItem
                                disabled={actionPending || !connected}
                                onClick={() => {
                                  void runAction(async () => {
                                    if (!await ensurePersistentDeviceRuntime()) {
                                      toast.error("Failed to leave temporary mode before resetting this profile");
                                      return;
                                    }
                                    const ok = await kbheDevice.resetProfileSlot(profile.slot);
                                    if (!ok) {
                                      toast.error("Failed to reset device profile");
                                      return;
                                    }
                                    const snapshot = await captureFirmwareProfileSnapshot(profile.slot);
                                    useProfileStore.getState().upsertDeviceProfileMirror(
                                      profile.slot,
                                      profile.name,
                                      true,
                                      snapshot ?? undefined,
                                    );
                                    if (!await kbheDevice.saveSettings()) {
                                      toast.error("Reset, but failed to persist device profile");
                                      return;
                                    }
                                    await syncRuntimeState();
                                    toast.success("Device profile reset to defaults");
                                  });
                                }}
                              >
                                <IconRotate className="size-4" />
                                Reset to Defaults
                              </DropdownMenuItem>
                            </>
                          )}

                          <DropdownMenuItem
                            disabled={actionPending}
                            onClick={() => openDuplicateDialog(profile)}
                          >
                            <IconCopy className="size-4" />
                            Duplicate
                          </DropdownMenuItem>

                          <DropdownMenuItem
                            disabled={
                              actionPending
                              || !connected
                            }
                            onClick={() => openDuplicateDialog(profile, profile.kind === "device" ? "app" : "device")}
                          >
                            <IconRefresh className="size-4" />
                            {profile.kind === "device" ? "Make App Profile" : "Make Device Profile"}
                          </DropdownMenuItem>

                          <DropdownMenuItem
                            disabled={actionPending || (profile.kind === "device" && !connected)}
                            onClick={() => {
                              setRenameTarget(profile);
                              setRenameValue(profile.name);
                            }}
                          >
                            <IconPencil className="size-4" />
                            Rename
                          </DropdownMenuItem>

                          {profile.kind === "app" && (
                            <>
                              <DropdownMenuSeparator />
                              <DropdownMenuItem onClick={() => handleExport(profile.name)}>
                                <IconDownload className="size-4" />
                                Export JSON
                              </DropdownMenuItem>
                              <DropdownMenuItem onClick={() => handleImportClick(profile.name)}>
                                <IconUpload className="size-4" />
                                Import JSON
                              </DropdownMenuItem>
                            </>
                          )}

                          <DropdownMenuSeparator />
                          <DropdownMenuItem
                            variant="destructive"
                            disabled={
                              actionPending
                              || (profile.kind === "device" && !connected)
                              || (profile.kind === "device" && !canDeleteDevice)
                            }
                            onClick={() => setDeleteTarget(profile)}
                          >
                            <IconTrash className="size-4" />
                            Delete
                          </DropdownMenuItem>
                        </DropdownMenuContent>
                      </DropdownMenu>
                    </div>

                    {!profile.isRuntimeActive && (
                      <Button
                        variant="outline"
                        size="sm"
                        className="w-full h-7 text-xs gap-1"
                        disabled={actionPending || (profile.kind === "device" && !connected)}
                        onClick={() => handleActivateProfile(profile)}
                      >
                        <IconRefresh className="size-3" />
                        {profile.kind === "device" ? "Use on Keyboard" : "Apply Temporary"}
                      </Button>
                    )}
                  </div>
                );
              })}

              <button
                type="button"
                onClick={() => {
                  setNewName("");
                  setCreateType("app");
                  setCreateOpen(true);
                }}
                className="rounded-lg border-2 border-dashed border-muted-foreground/25 p-4 flex flex-col items-center justify-center gap-2 text-muted-foreground hover:border-primary/40 hover:text-foreground transition-colors min-h-22 cursor-pointer"
              >
                <IconPlus className="size-5" />
                <span className="text-xs font-medium">New Profile</span>
              </button>
            </div>
          )}

          {profiles.length === 0 && (
            <div className="text-center py-12 text-muted-foreground text-sm">
              No profiles yet. Create one to get started.
            </div>
          )}
        </SectionCard>
      </PageContent>

      <input
        ref={importRef}
        type="file"
        accept="application/json"
        onChange={handleImportFile}
        className="hidden"
        aria-hidden="true"
      />

      <Dialog open={createOpen} onOpenChange={setCreateOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Create Profile</DialogTitle>
            <DialogDescription>
              Choose where the profile is stored.
            </DialogDescription>
          </DialogHeader>
          <Select
            value={createType}
            items={[
              { value: "app", label: "App Profile (inactive)" },
              { value: "device", label: "Device Profile (active slot)" },
            ]}
            onValueChange={(value) => setCreateType(value as ProfileTargetType)}
          >
            <SelectTrigger className="w-full">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectGroup>
                <SelectItem value="app">App Profile (inactive)</SelectItem>
                <SelectItem value="device">Device Profile (active slot)</SelectItem>
              </SelectGroup>
            </SelectContent>
          </Select>
          <Input
            placeholder="Profile name…"
            value={newName}
            onChange={(e) => setNewName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && handleCreate()}
            autoFocus
          />
          <DialogFooter>
            <Button variant="outline" onClick={() => setCreateOpen(false)}>Cancel</Button>
            <Button
              onClick={handleCreate}
              disabled={
                actionPending
                || !newName.trim()
                || (createType === "app" && existingAppNames.has(newName.trim()))
                || (createType === "device" && (!connected || freeDeviceSlot == null))
              }
            >
              <IconPlus className="size-4 mr-1" />
              Create
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={!!deleteTarget} onOpenChange={() => setDeleteTarget(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Delete profile?</DialogTitle>
            <DialogDescription>
              This will permanently delete &ldquo;{deleteTarget?.name ?? ""}&rdquo;. This cannot be undone.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" onClick={() => setDeleteTarget(null)}>Cancel</Button>
            <Button variant="destructive" disabled={actionPending} onClick={handleDelete}>
              Delete
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={!!duplicateTarget} onOpenChange={() => setDuplicateTarget(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Duplicate &ldquo;{duplicateTarget?.name ?? ""}&rdquo;</DialogTitle>
            <DialogDescription>
              Choose whether the duplicated profile should be stored on the keyboard (device) or in the app.
            </DialogDescription>
          </DialogHeader>
          <Select
            value={duplicateType}
            items={[
              { value: "device", label: "Device Profile" },
              { value: "app", label: "App Profile" },
            ]}
            onValueChange={(value) => {
              const nextType = value as ProfileTargetType;
              setDuplicateType(nextType);
              if (nextType === "app") {
                setDuplicateName(uniqueName(duplicateName || `${duplicateTarget?.name ?? "Profile"} copy`, existingAppNames));
              }
            }}
          >
            <SelectTrigger className="w-full">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectGroup>
                <SelectItem value="device" disabled={!connected || freeDeviceSlot == null}>Device Profile</SelectItem>
                <SelectItem value="app">App Profile</SelectItem>
              </SelectGroup>
            </SelectContent>
          </Select>
          <Input
            placeholder="New profile name…"
            value={duplicateName}
            onChange={(e) => setDuplicateName(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && duplicateName.trim()) {
                handleDuplicate();
              }
            }}
            autoFocus
          />
          <DialogFooter>
            <Button variant="outline" onClick={() => setDuplicateTarget(null)}>Cancel</Button>
            <Button
              onClick={handleDuplicate}
              disabled={
                actionPending
                || !duplicateName.trim()
                || (duplicateType === "app" && existingAppNames.has(duplicateName.trim()))
                || (duplicateType === "device" && (!connected || freeDeviceSlot == null))
              }
            >
              <IconCopy className="size-4 mr-1" />
              Duplicate
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={renameTarget !== null} onOpenChange={() => setRenameTarget(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Rename &ldquo;{renameTarget?.name ?? ""}&rdquo;</DialogTitle>
            <DialogDescription>
              Enter a new name for this profile.
            </DialogDescription>
          </DialogHeader>
          <Input
            placeholder="New name…"
            value={renameValue}
            onChange={(e) => setRenameValue(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && renameValue.trim()) {
                handleRename();
              }
            }}
            autoFocus
          />
          <DialogFooter>
            <Button variant="outline" onClick={() => setRenameTarget(null)}>Cancel</Button>
            <Button
              onClick={handleRename}
              disabled={
                actionPending ||
                !renameValue.trim() ||
                (renameTarget?.kind === "app"
                  && renameValue.trim() !== renameTarget.name
                  && existingAppNames.has(renameValue.trim()))
              }
            >
              <IconPencil className="size-4 mr-1" />
              Rename
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
