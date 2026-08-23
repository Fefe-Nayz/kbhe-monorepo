import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { getVersion } from "@tauri-apps/api/app";
import { isTauri } from "@tauri-apps/api/core";
import { useRef, useState } from "react";
import { PageContent, PageSection } from "@/components/shared/PageLayout";
import { SegmentedControl } from "@/components/shared/SegmentedControl";
import { FormRow, FormRows, SectionCard } from "@/components/shared/SectionCard";
import { useTheme } from "@/components/theme-provider";
import { Badge } from "@/components/ui/badge";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  getLaunchOnStartupEnabled,
  getWindowsMicaEnabled,
  getStartupPreferences,
  isWindowsMicaSupported,
  setLaunchOnStartupEnabled,
  setStartupPreferences,
  setWindowsMicaEnabled,
  STARTUP_WINDOW_MODE_OPTIONS,
  type StartupWindowMode,
} from "@/lib/app-startup";
import { checkAppUpdate, downloadAndRunAppInstaller } from "@/lib/kbhe/releases";
import { useDeviceSession } from "@/lib/kbhe/session";
import { IconAlertTriangle, IconDownload, IconRefresh } from "@tabler/icons-react";
import { toast } from "sonner";

type ThemeMode = "light" | "dark" | "system";

const APP_QUERY_KEYS = {
  startupPreferences: ["app", "startup-preferences"] as const,
  launchOnStartup: ["app", "launch-on-startup"] as const,
  release: ["app", "release"] as const,
  version: ["app", "version"] as const,
};

const RESETTABLE_LOCAL_STORAGE_KEYS = new Set([
  "keyboard-active-app-profile",
  "keyboard-active-profile",
]);

const RESETTABLE_LOCAL_STORAGE_PREFIXES = [
  "kbhe-",
  "keyboard-profile:",
  "keyboard-device-profile:",
];

function shouldResetLocalStorageKey(key: string): boolean {
  return RESETTABLE_LOCAL_STORAGE_KEYS.has(key)
    || RESETTABLE_LOCAL_STORAGE_PREFIXES.some((prefix) => key.startsWith(prefix));
}

function resetAppLocalStorage(): void {
  for (const key of Object.keys(localStorage)) {
    if (shouldResetLocalStorageKey(key)) {
      localStorage.removeItem(key);
    }
  }
}

const THEME_OPTIONS: Array<{ value: ThemeMode; label: string }> = [
  { value: "system", label: "System" },
  { value: "dark", label: "Dark" },
  { value: "light", label: "Light" },
];

export default function AppSettings() {
  const qc = useQueryClient();
  const developerMode = useDeviceSession((state) => state.developerMode);
  const setDeveloperMode = useDeviceSession((state) => state.setDeveloperMode);
  const { theme, resolvedTheme, setTheme } = useTheme();
  const micaSupported = isWindowsMicaSupported();
  const [micaEnabled, setMicaEnabled] = useState<boolean>(() => getWindowsMicaEnabled());
  const [resetDialogOpen, setResetDialogOpen] = useState(false);
  const appUpdateLaunchRef = useRef(false);

  const startupPrefsQ = useQuery({
    queryKey: APP_QUERY_KEYS.startupPreferences,
    queryFn: getStartupPreferences,
  });

  const launchOnStartupQ = useQuery({
    queryKey: APP_QUERY_KEYS.launchOnStartup,
    queryFn: getLaunchOnStartupEnabled,
  });

  const appUpdateQ = useQuery({
    queryKey: APP_QUERY_KEYS.release,
    queryFn: checkAppUpdate,
    enabled: isTauri(),
    refetchInterval: 60 * 60 * 1000,
    staleTime: 10 * 60 * 1000,
  });

  const appVersionQ = useQuery({
    queryKey: APP_QUERY_KEYS.version,
    queryFn: getVersion,
    enabled: isTauri(),
    staleTime: Infinity,
  });

  const startupModeMutation = useMutation({
    mutationFn: async (mode: StartupWindowMode) => {
      await setStartupPreferences({ startupMode: mode });
    },
    onSuccess: async () => {
      await qc.invalidateQueries({ queryKey: APP_QUERY_KEYS.startupPreferences });
      toast.success("Startup window mode updated.");
    },
    onError: (error) => {
      const message = error instanceof Error ? error.message : "Failed to update startup mode.";
      toast.error(message);
    },
  });

  const launchOnStartupMutation = useMutation({
    mutationFn: async (enabled: boolean) => {
      await setLaunchOnStartupEnabled(enabled);
    },
    onSuccess: async (_, enabled) => {
      await qc.invalidateQueries({ queryKey: APP_QUERY_KEYS.launchOnStartup });
      toast.success(enabled ? "Launch on startup enabled." : "Launch on startup disabled.");
    },
    onError: (error) => {
      const message = error instanceof Error ? error.message : "Failed to update launch on startup.";
      toast.error(message);
    },
  });

  const appUpdateMutation = useMutation({
    mutationFn: async (tag: string) => {
      await downloadAndRunAppInstaller(tag);
    },
    onSuccess: () => {
      toast.success("Installer launched.");
    },
    onError: (error) => {
      const message = error instanceof Error ? error.message : "Failed to install update.";
      toast.error(message);
    },
  });

  const startupMode = startupPrefsQ.data?.startupMode ?? "normal";

  const resetAppMutation = useMutation({
    mutationFn: async () => {
      // Best effort: restore startup behavior before wiping local UI state.
      try {
        await setLaunchOnStartupEnabled(false);
      } catch {
        // Ignore autostart reset failures and continue with local reset.
      }

      try {
        await setStartupPreferences({ startupMode: "normal" });
      } catch {
        // Ignore startup-mode reset failures and continue with local reset.
      }

      resetAppLocalStorage();
    },
    onSuccess: () => {
      qc.clear();
      toast.success("Application reset. Reloading...");
      window.setTimeout(() => {
        window.location.reload();
      }, 180);
    },
    onError: (error) => {
      const message = error instanceof Error ? error.message : "Failed to reset app data.";
      toast.error(message);
    },
  });

  return (
    <PageContent containerClassName="max-w-3xl">
      {isTauri() && (
        <SectionCard
          tone={appUpdateQ.data?.updateAvailable ? "accent" : "default"}
          title="Application update"
          icon={<IconDownload />}
          description={
            appUpdateQ.data?.updateAvailable
              ? `Release ${appUpdateQ.data.tag ?? appUpdateQ.data.version} is available.`
              : appUpdateQ.isLoading
                ? "Checking GitHub releases…"
                : "You are running the latest release."
          }
          headerRight={
            <Button
              variant="ghost"
              size="icon-sm"
              disabled={appUpdateQ.isFetching || appUpdateMutation.isPending}
              onClick={() => {
                void appUpdateQ.refetch();
              }}
              title="Check again"
            >
              <IconRefresh className="size-4" />
            </Button>
          }
        >
          <FormRow
            label="Latest installer"
            description={appUpdateQ.data?.assetName ?? "No installer update available."}
          >
            {appUpdateQ.data?.version && (
              <Badge variant="secondary" className="font-mono">
                {appUpdateQ.data.version}
              </Badge>
            )}
            <Button
              disabled={
                !appUpdateQ.data?.tag
                || !appUpdateQ.data.updateAvailable
                || appUpdateMutation.isPending
              }
              onClick={() => {
                const tag = appUpdateQ.data?.tag;
                if (!tag || appUpdateLaunchRef.current) return;
                appUpdateLaunchRef.current = true;
                appUpdateMutation.mutate(tag, {
                  onSettled: () => {
                    appUpdateLaunchRef.current = false;
                  },
                });
              }}
            >
              <IconDownload className="size-4" />
              {appUpdateMutation.isPending ? "Downloading…" : "Install update"}
            </Button>
          </FormRow>
        </SectionCard>
      )}

      <PageSection title="Appearance">
        <SectionCard>
          <FormRows>
            <FormRow
              label="Theme"
              description={`Following your choice, currently rendering in ${
                resolvedTheme === "dark" ? "dark" : "light"
              } mode.`}
            >
              <SegmentedControl
                aria-label="Theme"
                value={theme}
                size="md"
                onChange={(value) => setTheme(value as ThemeMode)}
                options={THEME_OPTIONS.map((option) => ({
                  value: option.value,
                  label: option.label,
                }))}
              />
            </FormRow>

            <FormRow
              label="Translucent sidebar"
              description={
                micaSupported
                  ? "Use the Windows Mica material behind the sidebar."
                  : "Only available in the Windows desktop app."
              }
            >
              <Switch
                checked={micaEnabled}
                disabled={!micaSupported}
                onCheckedChange={(value) => {
                  setWindowsMicaEnabled(value);
                  setMicaEnabled(value);
                  toast.success(value ? "Windows Mica enabled." : "Windows Mica disabled.");
                }}
              />
            </FormRow>
          </FormRows>
        </SectionCard>
      </PageSection>

      <PageSection title="Startup">
        <SectionCard>
          <FormRows>
            <FormRow
              label="Launch on system startup"
              description="Start the configurator automatically when you sign in."
            >
              <Switch
                checked={launchOnStartupQ.data ?? false}
                disabled={launchOnStartupQ.isLoading || launchOnStartupMutation.isPending}
                onCheckedChange={(value) => launchOnStartupMutation.mutate(value)}
              />
            </FormRow>
            <FormRow
              label="Window mode at launch"
              description="How the window appears when the app starts."
            >
              <Select
                value={startupMode}
                items={STARTUP_WINDOW_MODE_OPTIONS}
                disabled={startupPrefsQ.isLoading || startupModeMutation.isPending}
                onValueChange={(value) => startupModeMutation.mutate(value as StartupWindowMode)}
              >
                <SelectTrigger className="h-8 w-44">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectGroup>
                    {STARTUP_WINDOW_MODE_OPTIONS.map((option) => (
                      <SelectItem key={option.value} value={option.value}>
                        {option.label}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
            </FormRow>
          </FormRows>
        </SectionCard>
      </PageSection>

      <PageSection title="Advanced">
        <SectionCard>
          <FormRows>
            <FormRow
              label="Developer mode"
              description="Adds the Diagnostics page and firmware developer options to the sidebar."
            >
              <Switch checked={developerMode} onCheckedChange={(value) => setDeveloperMode(value)} />
            </FormRow>
            <FormRow
              label="App version"
              description="The build of kbhe-configurator currently running."
            >
              <Badge variant="secondary" className="font-mono">
                {isTauri()
                  ? appVersionQ.data ?? (appVersionQ.isLoading ? "Loading…" : "Unknown")
                  : "Web preview"}
              </Badge>
            </FormRow>
          </FormRows>
        </SectionCard>
      </PageSection>

      <PageSection title="Danger zone">
        <SectionCard
          tone="danger"
          title="Reset the application"
          description="Clears local profiles, appearance settings and developer preferences, then reloads. Your keyboard is not touched."
          icon={<IconAlertTriangle />}
          footer={
            <Button
              variant="destructive"
              size="sm"
              onClick={() => setResetDialogOpen(true)}
              disabled={resetAppMutation.isPending}
            >
              Reset app data
            </Button>
          }
        >
          <p className="text-xs leading-relaxed text-muted-foreground">
            Settings stored on the keyboard itself — keymaps, calibration, device profiles —
            stay exactly as they are. Use the Device page for a factory reset of the hardware.
          </p>
        </SectionCard>
      </PageSection>

      <AlertDialog open={resetDialogOpen} onOpenChange={setResetDialogOpen}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <div className="mb-2 inline-flex size-10 items-center justify-center rounded-md bg-destructive/10 text-destructive">
              <IconAlertTriangle className="size-5" />
            </div>
            <AlertDialogTitle>Reset application data?</AlertDialogTitle>
            <AlertDialogDescription>
              This removes local profiles and app preferences, resets startup options and
              reloads the app. It cannot be undone.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel disabled={resetAppMutation.isPending}>Cancel</AlertDialogCancel>
            <AlertDialogAction
              variant="destructive"
              disabled={resetAppMutation.isPending}
              onClick={() => resetAppMutation.mutate()}
            >
              {resetAppMutation.isPending ? "Resetting…" : "Reset app data"}
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </PageContent>
  );
}
