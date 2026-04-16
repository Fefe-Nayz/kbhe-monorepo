import { useEffect } from "react";
import { useProfileStore } from "@/stores/profileStore";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import {
  activateDeviceRuntimeProfile,
  activateTemporaryAppProfile,
  hydrateProfileStoreFromSession,
} from "@/lib/kbhe/profile-runtime";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectLabel,
  SelectSeparator,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { IconLayoutGrid } from "@tabler/icons-react";

export function ProfileSelect() {
  const appProfiles = useProfileStore((s) => s.appProfiles);
  const runtimeSource = useProfileStore((s) => s.runtimeSource);
  const activeAppProfileName = useProfileStore((s) => s.activeAppProfileName);
  const activeDeviceSlot = useProfileStore((s) => s.activeDeviceSlot);
  const setRuntimeSource = useProfileStore((s) => s.setRuntimeSource);
  const init = useProfileStore((s) => s.init);
  const status = useDeviceSession((s) => s.status);
  const activeProfileIndex = useDeviceSession((s) => s.activeProfileIndex);
  const profileNames = useDeviceSession((s) => s.profileNames);
  const profileUsedMask = useDeviceSession((s) => s.profileUsedMask);

  const connected = status === "connected";

  useEffect(() => {
    init();
  }, [init]);

  useEffect(() => {
    if (!connected) return;
    void DeviceSessionManager.refreshRuntimeProfileState();
  }, [connected]);

  useEffect(() => {
    if (!connected) return;
    hydrateProfileStoreFromSession();
  }, [activeProfileIndex, connected, profileNames, profileUsedMask]);

  const deviceProfiles = profileNames.map((name, slot) => ({
    slot,
    name,
    used: Boolean(profileUsedMask & (1 << slot)),
  }));

  const selectedValue = runtimeSource === "app" && activeAppProfileName
    ? `app:${activeAppProfileName}`
    : `device:${activeDeviceSlot ?? activeProfileIndex ?? -1}`;

  const items = [
    ...deviceProfiles.map((profile) => ({
      value: `device:${profile.slot}`,
      label: profile.name,
    })),
    ...appProfiles.map((profile) => ({
      value: `app:${profile.name}`,
      label: profile.name,
    })),
  ];

  const handleValueChange = (value: string) => {
    if (value.startsWith("device:")) {
      const slot = Number.parseInt(value.replace("device:", ""), 10);
      if (!Number.isFinite(slot)) return;

      setRuntimeSource("device");
      if (!connected) return;

      void activateDeviceRuntimeProfile(slot);
      return;
    }

    if (value.startsWith("app:")) {
      const profileName = value.replace("app:", "");
      if (!profileName) return;
      void activateTemporaryAppProfile(profileName);
    }
  };

  return (
    <Select
      value={selectedValue}
      items={items}
      onValueChange={(v) => handleValueChange(v as string)}
    >
      <SelectTrigger className="gap-1.5 text-xs font-medium">
        <IconLayoutGrid className="size-3.5 text-muted-foreground" />
        <SelectValue />
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          <SelectLabel>Device</SelectLabel>
          {deviceProfiles.map((profile) => (
            <SelectItem key={profile.slot} value={`device:${profile.slot}`} disabled={!connected || !profile.used}>
              {profile.name}
            </SelectItem>
          ))}
        </SelectGroup>
        <SelectSeparator />
        <SelectGroup>
          <SelectLabel>App</SelectLabel>
          {appProfiles.map((profile) => (
            <SelectItem key={profile.id} value={`app:${profile.name}`}>
              {profile.name}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </Select>
  );
}
