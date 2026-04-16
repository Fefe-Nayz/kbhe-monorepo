import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { useState } from "react";
import { PageContent } from "@/components/shared/PageLayout";
import { FormRow, SectionCard } from "@/components/shared/SectionCard";
import { useTheme } from "@/components/theme-provider";
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
import { useDeviceSession } from "@/lib/kbhe/session";
import { IconAlertTriangle } from "@tabler/icons-react";
import { toast } from "sonner";

type ThemeMode = "light" | "dark" | "system";

const APP_QUERY_KEYS = {
  startupPreferences: ["app", "startup-preferences"] as const,
  launchOnStartup: ["app", "launch-on-startup"] as const,
};

const RESETTABLE_LOCAL_STORAGE_KEYS = new Set([
  "keyboard-active-app-profile",
  "keyboard-active-profile",
]);

const RESETTABLE_LOCAL_STORAGE_PREFIXES = [
  "kbhe-",
  "keyboard-profile:",
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

  const startupPrefsQ = useQuery({
    queryKey: APP_QUERY_KEYS.startupPreferences,
    queryFn: getStartupPreferences,
  });

  const launchOnStartupQ = useQuery({
    queryKey: APP_QUERY_KEYS.launchOnStartup,
    queryFn: getLaunchOnStartupEnabled,
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
    <div className="flex h-full flex-col overflow-hidden">
      <PageContent containerClassName="max-w-2xl">
        <SectionCard
          title="Appearance"
          description="Personalize how the configurator looks on this machine."
        >
          <div className="flex flex-col divide-y">
            <FormRow
              label="Theme"
              description={`Current resolved theme: ${resolvedTheme === "dark" ? "Dark" : "Light"}`}
            >
              <Select value={theme} onValueChange={(value) => setTheme(value as ThemeMode)}>
                <SelectTrigger className="h-8 w-36">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectGroup>
                    {THEME_OPTIONS.map((option) => (
                      <SelectItem key={option.value} value={option.value}>
                        {option.label}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
            </FormRow>

            <FormRow
              label="Windows Mica Sidebar"
              description={
                micaSupported
                  ? "Enable translucent Mica material for the sidebar background"
                  : "Available only in the Windows Tauri app"
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
          </div>
        </SectionCard>

        <SectionCard
          title="Startup"
          description="Control how the app launches with your operating system."
        >
          <div className="flex flex-col divide-y">
            <FormRow
              label="Launch on System Startup"
              description="Automatically launch the app when your OS starts"
            >
              <Switch
                checked={launchOnStartupQ.data ?? false}
                disabled={launchOnStartupQ.isLoading || launchOnStartupMutation.isPending}
                onCheckedChange={(value) => launchOnStartupMutation.mutate(value)}
              />
            </FormRow>
            <FormRow label="Startup Window Mode" description="How the app opens when launched">
              <Select
                value={startupMode}
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
          </div>
        </SectionCard>

        <SectionCard
          title="Developer"
          description="Enable advanced tools and firmware diagnostics pages."
        >
          <FormRow label="Developer Mode" description="Shows diagnostics and firmware developer options">
            <Switch checked={developerMode} onCheckedChange={(value) => setDeveloperMode(value)} />
          </FormRow>
        </SectionCard>

        <SectionCard
          title="Danger Zone"
          description="Erase local app data and return settings to defaults."
        >
          <FormRow
            label="Reset Application"
            description="Clears local profiles, appearance settings, and developer preferences."
          >
            <Button
              variant="destructive"
              onClick={() => setResetDialogOpen(true)}
              disabled={resetAppMutation.isPending}
            >
              Reset App
            </Button>
          </FormRow>
        </SectionCard>

        <AlertDialog open={resetDialogOpen} onOpenChange={setResetDialogOpen}>
          <AlertDialogContent>
            <AlertDialogHeader>
              <div className="mb-2 inline-flex size-10 items-center justify-center rounded-md bg-destructive/10 text-destructive">
                <IconAlertTriangle className="size-5" />
              </div>
              <AlertDialogTitle>Reset application data?</AlertDialogTitle>
              <AlertDialogDescription>
                This will remove local profiles and app preferences, reset startup options, and reload the app.
                This action cannot be undone.
              </AlertDialogDescription>
            </AlertDialogHeader>
            <AlertDialogFooter>
              <AlertDialogCancel disabled={resetAppMutation.isPending}>Cancel</AlertDialogCancel>
              <AlertDialogAction
                variant="destructive"
                disabled={resetAppMutation.isPending}
                onClick={() => resetAppMutation.mutate()}
              >
                {resetAppMutation.isPending ? "Resetting..." : "Reset App"}
              </AlertDialogAction>
            </AlertDialogFooter>
          </AlertDialogContent>
        </AlertDialog>
      </PageContent>
    </div>
  );
}
