import { lazy, Suspense, useEffect, useState } from "react";
import { Routes, Route } from "react-router-dom";

import { SidebarProvider, SidebarInset, SidebarTrigger } from "@/components/ui/sidebar";
import { TooltipProvider } from "@/components/ui/tooltip";
import { Skeleton } from "@/components/ui/skeleton";
import { AppSidebar } from "@/components/app-sidebar";
import { DeviceBanner } from "@/components/DeviceBanner";
import { ThemeButton } from "@/components/nav-components/themeButton";
import { ProfileSelect } from "@/components/profile-select";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { IconDeviceDesktop } from "@tabler/icons-react";

const Dashboard    = lazy(() => import("@/pages/Dashboard"));
const Profiles     = lazy(() => import("@/pages/Profiles"));
const Keymap       = lazy(() => import("@/pages/Keymap"));
const Performance  = lazy(() => import("@/pages/Performance"));
const AdvancedKeys = lazy(() => import("@/pages/AdvancedKeys"));
const Gamepad      = lazy(() => import("@/pages/Gamepad"));
const Calibration  = lazy(() => import("@/pages/Calibration"));
const Lighting     = lazy(() => import("@/pages/Lighting"));
const Rotary       = lazy(() => import("@/pages/Rotary"));
const Device       = lazy(() => import("@/pages/Device"));
const Firmware     = lazy(() => import("@/pages/Firmware"));
const Diagnostics  = lazy(() => import("@/pages/Diagnostics"));

const MIN_WIDTH  = 1024;
const MIN_HEIGHT = 768;

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

function PageFallback() {
  return (
    <div className="flex flex-1 flex-col gap-4 p-4">
      <Skeleton className="h-8 w-48" />
      <Skeleton className="h-64 w-full" />
      <div className="grid grid-cols-2 gap-4">
        <Skeleton className="h-32" />
        <Skeleton className="h-32" />
      </div>
    </div>
  );
}

function AppHeader() {
  return (
    <header className="flex h-12 shrink-0 items-center gap-2 border-b px-4">
      <SidebarTrigger className="-ml-1" />
      <div className="h-4 w-px shrink-0 bg-border" />
      <ProfileSelect />
      <div className="flex-1" />
      <ThemeButton />
    </header>
  );
}

export function AppShell() {
  const [tooSmall, setTooSmall] = useState(
    () => window.innerWidth < MIN_WIDTH || window.innerHeight < MIN_HEIGHT,
  );
  const developerMode = useDeviceSession((s) => s.developerMode);

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
      <SidebarProvider>
        <AppSidebar />
        <SidebarInset className="flex flex-col min-h-0 overflow-hidden">
          <AppHeader />
          <DeviceBanner />
          <main className="flex flex-1 flex-col min-h-0 overflow-hidden">
            <Suspense fallback={<PageFallback />}>
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
                {developerMode && (
                  <Route path="/diagnostics" element={<Diagnostics />} />
                )}
              </Routes>
            </Suspense>
          </main>
        </SidebarInset>
      </SidebarProvider>
    </TooltipProvider>
  );
}
