import { useEffect, useState, type CSSProperties } from "react";
import { Routes, Route, useLocation } from "react-router-dom";

import { SidebarProvider, SidebarInset, SidebarTrigger } from "@/components/ui/sidebar";
import { TooltipProvider } from "@/components/ui/tooltip";
import { Separator } from "@/components/ui/separator";
import { AppSidebar } from "@/components/app-sidebar";
import { DeviceBanner } from "@/components/DeviceBanner";
import { UpdateBanner } from "@/components/UpdateBanner";
import { ThemeButton } from "@/components/nav-components/themeButton";
import { ProfileSelect } from "@/components/profile-select";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { useAudioSpectrumService } from "@/lib/kbhe/useAudioSpectrumService";
import { useAlphaMaskService } from "@/lib/kbhe/useAlphaMaskService";
import { useCloseLightingService } from "@/lib/kbhe/useCloseLightingService";
import Dashboard from "@/pages/Dashboard";
import Profiles from "@/pages/Profiles";
import Keymap from "@/pages/Keymap";
import Performance from "@/pages/performance";
import AdvancedKeys from "@/pages/AdvancedKeys";
import Gamepad from "@/pages/Gamepad";
import Calibration from "@/pages/calibration";
import Lighting from "@/pages/Lighting";
import Rotary from "@/pages/Rotary";
import Device from "@/pages/Device";
import Firmware from "@/pages/Firmware";
import Diagnostics from "@/pages/Diagnostics";
import AppSettings from "@/pages/AppSettings";
import { IconDeviceDesktop } from "@tabler/icons-react";

const MIN_WIDTH  = 1024;
const MIN_HEIGHT = 768;

const PAGE_TITLES: Record<string, string> = {
  "/": "Dashboard",
  "/profiles": "Profiles",
  "/keymap": "Keymap",
  "/performance": "Performance",
  "/advanced-keys": "Advanced Keys",
  "/gamepad": "Gamepad",
  "/calibration": "Calibration",
  "/lighting": "Lighting",
  "/rotary": "Rotary Encoder",
  "/device": "Device",
  "/firmware": "Firmware",
  "/settings": "App Settings",
  "/diagnostics": "Diagnostics",
};

function getPageTitle(pathname: string): string {
  return PAGE_TITLES[pathname] ?? "KBHE Configurator";
}

function TooSmallScreen() {
  return (
    <div className="flex min-h-screen flex-col items-center justify-center gap-4 text-muted-foreground px-6 text-center">
      <IconDeviceDesktop className="size-20" />
      <p className="text-lg font-medium max-w-sm">
        Window too small. Please resize or zoom out (min {MIN_WIDTH}&times;{MIN_HEIGHT}).
      </p>
    </div>
  );
}

function AppHeader({ title }: { title: string }) {
  return (
    <header className="flex h-(--header-height) shrink-0 items-center border-b transition-[width,height] ease-linear group-has-data-[collapsible=icon]/sidebar-wrapper:h-(--header-height)">
      <div className="flex w-full items-center gap-1 px-4 lg:gap-2 lg:px-6">
        <SidebarTrigger className="-ml-1" />
        <Separator orientation="vertical" className="mx-2 h-4 data-vertical:self-auto" />
        <h1 className="text-sm font-semibold tracking-tight truncate">{title}</h1>
        <div className="flex-1" />
        <ProfileSelect />
        <ThemeButton />
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
  const pageTitle = getPageTitle(location.pathname);
  useAudioSpectrumService();
  useAlphaMaskService();
  useCloseLightingService();

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
      <SidebarProvider
        style={
          {
            "--sidebar-width": "calc(var(--spacing) * 72)",
            "--header-height": "calc(var(--spacing) * 12)",
          } as CSSProperties
        }
      >
        <AppSidebar variant="inset" />
        <SidebarInset className="flex min-w-0 flex-col min-h-0 overflow-hidden">
          <AppHeader title={pageTitle} />
          <DeviceBanner />
          <UpdateBanner />
          <main className="flex min-w-0 flex-1 flex-col min-h-0 overflow-hidden">
            <Routes>
              <Route path="/"              element={<Dashboard />} />
              <Route path="/profiles"      element={<Profiles />} />
              <Route path="/keymap"        element={<Keymap />} />
              <Route path="/performance"   element={<Performance />} />
              <Route path="/advanced-keys" element={<AdvancedKeys />} />
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
