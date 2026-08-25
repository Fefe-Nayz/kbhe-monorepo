import { useEffect, useState, useSyncExternalStore, type CSSProperties } from "react";
import { Routes, Route, useLocation } from "react-router-dom";

import { SidebarProvider, SidebarInset, SidebarTrigger } from "@/components/ui/sidebar";
import { TooltipProvider } from "@/components/ui/tooltip";
import { Separator } from "@/components/ui/separator";
import { AppSidebar } from "@/components/app-sidebar";
import { DeviceBanner, DeviceStatusChip } from "@/components/DeviceBanner";
import { UpdateBanner } from "@/components/UpdateBanner";
import { CompatibilityBanner } from "@/components/CompatibilityBanner";
import { ThemeButton } from "@/components/nav-components/themeButton";
import { ProfileSelect } from "@/components/profile-select";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import {
  getProfileOperationPending,
  subscribeProfileOperationPending,
} from "@/lib/kbhe/profile-operation-lock";
import { useAudioSpectrumService } from "@/lib/kbhe/useAudioSpectrumService";
import { useAlphaMaskService } from "@/lib/kbhe/useAlphaMaskService";
import { useDashboardMcuTrendsService } from "@/lib/kbhe/useDashboardMcuTrendsService";
import Dashboard from "@/pages/Dashboard";
import Profiles from "@/pages/Profiles";
import Keymap from "@/pages/Keymap";
import Performance from "@/pages/performance";
import AdvancedKeys from "@/pages/AdvancedKeys";
import Macros from "@/pages/Macros";
import Gamepad from "@/pages/Gamepad";
import Calibration from "@/pages/calibration";
import Lighting from "@/pages/Lighting";
import Rotary from "@/pages/Rotary";
import Device from "@/pages/Device";
import Firmware from "@/pages/Firmware";
import Diagnostics from "@/pages/Diagnostics";
import AppSettings from "@/pages/AppSettings";
import {
  IconActivity,
  IconArrowBigUpLines,
  IconBraces,
  IconBrandSpeedtest,
  IconBulb,
  IconCrosshair,
  IconDeviceDesktop,
  IconDeviceGamepad2,
  IconHome,
  IconKeyboard,
  IconLayoutGrid,
  IconLoader2,
  IconRotateClockwise,
  IconSettings,
  IconUpload,
  type Icon,
} from "@tabler/icons-react";

const MIN_WIDTH  = 1024;
const MIN_HEIGHT = 768;

interface PageMeta {
  title: string;
  /** Short line under the title in the header — what the page is for. */
  subtitle: string;
  icon: Icon;
}

const PAGE_META: Record<string, PageMeta> = {
  "/": { title: "Dashboard", subtitle: "Device overview and live telemetry", icon: IconHome },
  "/profiles": { title: "Profiles", subtitle: "Device slots and saved app profiles", icon: IconLayoutGrid },
  "/keymap": { title: "Keymap", subtitle: "Assign keycodes per layer", icon: IconKeyboard },
  "/performance": { title: "Performance", subtitle: "Actuation, rapid trigger and travel", icon: IconBrandSpeedtest },
  "/advanced-keys": { title: "Advanced Keys", subtitle: "SOCD, tap-hold, toggle and dynamic keys", icon: IconArrowBigUpLines },
  "/macros": { title: "Macros", subtitle: "On-device programs and mode overlays", icon: IconBraces },
  "/gamepad": { title: "Gamepad", subtitle: "Controller output and analog curves", icon: IconDeviceGamepad2 },
  "/calibration": { title: "Calibration", subtitle: "Per-key sensor range tuning", icon: IconCrosshair },
  "/lighting": { title: "Lighting", subtitle: "RGB effects and per-key matrix", icon: IconBulb },
  "/rotary": { title: "Rotary Encoder", subtitle: "Encoder bindings per layer", icon: IconRotateClockwise },
  "/device": { title: "Device", subtitle: "Identity, input modes and maintenance", icon: IconSettings },
  "/firmware": { title: "Firmware", subtitle: "Flash and verify device firmware", icon: IconUpload },
  "/settings": { title: "App Settings", subtitle: "Appearance, startup and developer tools", icon: IconDeviceDesktop },
  "/diagnostics": { title: "Diagnostics", subtitle: "Live sensor data and firmware internals", icon: IconActivity },
};

const FALLBACK_META: PageMeta = {
  title: "KBHE Configurator",
  subtitle: "",
  icon: IconKeyboard,
};

function getPageMeta(pathname: string): PageMeta {
  return PAGE_META[pathname] ?? FALLBACK_META;
}

function TooSmallScreen() {
  return (
    <div className="flex min-h-screen flex-col items-center justify-center gap-5 px-6 text-center">
      <div className="flex size-16 items-center justify-center rounded-2xl bg-muted text-muted-foreground">
        <IconDeviceDesktop className="size-8" />
      </div>
      <div className="max-w-sm space-y-1.5">
        <p className="text-base font-semibold">Window is too small</p>
        <p className="text-sm text-muted-foreground">
          KBHE Configurator needs at least {MIN_WIDTH}&times;{MIN_HEIGHT}. Resize the window or
          zoom out to continue.
        </p>
      </div>
    </div>
  );
}

function AppHeader({ meta }: { meta: PageMeta }) {
  const Icon = meta.icon;
  return (
    <header className="flex h-(--header-height) shrink-0 items-center border-b bg-background/70 backdrop-blur-sm">
      <div className="flex w-full min-w-0 items-center gap-2 px-4 lg:px-5">
        <SidebarTrigger className="-ml-1 shrink-0" />
        <Separator orientation="vertical" className="mx-1 h-4 shrink-0 data-vertical:self-auto" />
        <div className="flex min-w-0 items-center gap-2">
          <Icon className="size-4 shrink-0 text-muted-foreground" />
          <h1 className="truncate text-sm font-semibold tracking-tight">{meta.title}</h1>
          {meta.subtitle && (
            <>
              <span className="hidden shrink-0 text-muted-foreground/40 xl:inline">/</span>
              <span className="hidden truncate text-xs text-muted-foreground xl:inline">
                {meta.subtitle}
              </span>
            </>
          )}
        </div>
        <div className="flex-1" />
        <div className="flex shrink-0 items-center gap-2">
          <DeviceStatusChip />
          <Separator orientation="vertical" className="h-4 data-vertical:self-auto" />
          <ProfileSelect />
          <ThemeButton />
        </div>
      </div>
    </header>
  );
}

export function AppShell() {
  const location = useLocation();
  const [tooSmall, setTooSmall] = useState(
    () => window.innerWidth < MIN_WIDTH || window.innerHeight < MIN_HEIGHT,
  );
  const developerMode = useDeviceSession((s) => s.developerMode);
  const profileOperationPending = useSyncExternalStore(
    subscribeProfileOperationPending,
    getProfileOperationPending,
    getProfileOperationPending,
  );
  const pageMeta = getPageMeta(location.pathname);
  useAudioSpectrumService();
  useAlphaMaskService();
  useDashboardMcuTrendsService();

  useEffect(() => {
    const handler = () => {
      setTooSmall(window.innerWidth < MIN_WIDTH || window.innerHeight < MIN_HEIGHT);
    };
    window.addEventListener("resize", handler);
    return () => window.removeEventListener("resize", handler);
  }, []);

  useEffect(() => {
    void DeviceSessionManager.init();
  }, []);

  if (tooSmall) return <TooSmallScreen />;

  return (
    <TooltipProvider>
      {profileOperationPending && (
        <div
          className="fixed inset-0 z-[100] flex cursor-wait items-start justify-center bg-background/40 pt-20 backdrop-blur-[1px]"
          role="status"
          aria-live="polite"
        >
          <div className="flex items-center gap-2.5 rounded-lg border bg-popover px-4 py-2.5 text-sm shadow-lg">
            <IconLoader2 className="size-4 animate-spin text-primary" />
            Synchronizing profile…
          </div>
        </div>
      )}
      <SidebarProvider
        inert={profileOperationPending ? true : undefined}
        style={
          {
            "--sidebar-width": "calc(var(--spacing) * 64)",
            "--header-height": "calc(var(--spacing) * 13)",
          } as CSSProperties
        }
      >
        <AppSidebar variant="inset" />
        <SidebarInset className="flex min-w-0 flex-col min-h-0 overflow-hidden">
          <AppHeader meta={pageMeta} />
          <DeviceBanner />
          <CompatibilityBanner />
          <UpdateBanner />
          <main className="flex min-w-0 flex-1 flex-col min-h-0 overflow-hidden">
            <Routes>
              <Route path="/"              element={<Dashboard />} />
              <Route path="/profiles"      element={<Profiles />} />
              <Route path="/keymap"        element={<Keymap />} />
              <Route path="/performance"   element={<Performance />} />
              <Route path="/advanced-keys" element={<AdvancedKeys />} />
              <Route path="/macros"        element={<Macros />} />
              <Route path="/gamepad"       element={<Gamepad />} />
              <Route path="/calibration"   element={<Calibration />} />
              <Route path="/lighting"      element={<Lighting />} />
              <Route path="/rotary"        element={<Rotary />} />
              <Route path="/device"        element={<Device />} />
              <Route path="/firmware"      element={<Firmware />} />
              <Route path="/settings"      element={<AppSettings />} />
              {developerMode && (
                <Route path="/diagnostics" element={<Diagnostics />} />
              )}
            </Routes>
          </main>
        </SidebarInset>
      </SidebarProvider>
    </TooltipProvider>
  );
}
