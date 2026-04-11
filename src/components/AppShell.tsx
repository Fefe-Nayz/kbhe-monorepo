import { lazy, Suspense, useEffect, useState } from "react";
import { Routes, Route } from "react-router-dom";

import { SidebarProvider, SidebarInset, SidebarTrigger } from "@/components/ui/sidebar";
import { Separator } from "@/components/ui/separator";
import { AppSidebar } from "@/components/app-sidebar";
import { DeviceBanner, DeviceStatusChip } from "@/components/DeviceBanner";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { IconDeviceDesktop } from "@tabler/icons-react";

// ── Page lazy imports ────────────────────────────────────────────────────────
const Dashboard      = lazy(() => import("@/pages/Dashboard"));
const Profiles       = lazy(() => import("@/pages/Profiles"));
const Keymap         = lazy(() => import("@/pages/Keymap"));
const Performance    = lazy(() => import("@/pages/Performance"));
const AdvancedKeys   = lazy(() => import("@/pages/AdvancedKeys"));
const Gamepad        = lazy(() => import("@/pages/Gamepad"));
const Calibration    = lazy(() => import("@/pages/Calibration"));
const Lighting       = lazy(() => import("@/pages/Lighting"));
const Rotary         = lazy(() => import("@/pages/Rotary"));
const Device         = lazy(() => import("@/pages/Device"));
const Diagnostics    = lazy(() => import("@/pages/Diagnostics"));

const MIN_WIDTH  = 1024;
const MIN_HEIGHT = 768;

function TooSmallScreen() {
  return (
    <div className="flex min-h-screen flex-col items-center justify-center gap-4 text-muted-foreground px-6 text-center">
      <IconDeviceDesktop className="size-20" />
      <p className="text-lg font-medium max-w-sm">
        Window too small. Please resize or zoom out (min {MIN_WIDTH}×{MIN_HEIGHT}).
      </p>
    </div>
  );
}

function PageFallback() {
  return (
    <div className="flex flex-1 items-center justify-center text-muted-foreground text-sm">
      Loading…
    </div>
  );
}

function AppHeader() {
  const { firmwareVersion } = useDeviceSession();

  return (
    <header className="flex h-12 shrink-0 items-center gap-3 border-b px-4">
      <div className="flex flex-1 items-center gap-2">
        <SidebarTrigger className="-ml-1" />
        <Separator orientation="vertical" className="h-4" />
        {/* Profile selector lives here — rendered by each page for now */}
      </div>
      <div className="flex shrink-0 items-center gap-3">
        {firmwareVersion && (
          <span className="text-xs text-muted-foreground hidden sm:block">
            fw {firmwareVersion}
          </span>
        )}
        <DeviceStatusChip />
      </div>
    </header>
  );
}

export function AppShell() {
  const [width, setWidth]   = useState(window.innerWidth);
  const [height, setHeight] = useState(window.innerHeight);
  const developerMode = useDeviceSession((s) => s.developerMode);

  useEffect(() => {
    const handler = () => {
      setWidth(window.innerWidth);
      setHeight(window.innerHeight);
    };
    window.addEventListener("resize", handler);
    return () => window.removeEventListener("resize", handler);
  }, []);

  // Boot device session once
  useEffect(() => {
    void DeviceSessionManager.init();
  }, []);

  if (width < MIN_WIDTH || height < MIN_HEIGHT) {
    return <TooSmallScreen />;
  }

  return (
    <SidebarProvider>
      <AppSidebar />
      <SidebarInset className="flex flex-col min-h-0 overflow-hidden">
        <AppHeader />
        <DeviceBanner />
        <main className="flex flex-1 flex-col min-h-0 overflow-hidden">
          <Suspense fallback={<PageFallback />}>
            <Routes>
              <Route path="/"                element={<Dashboard />} />
              <Route path="/profiles"        element={<Profiles />} />
              <Route path="/keymap"          element={<Keymap />} />
              <Route path="/performance"     element={<Performance />} />
              <Route path="/advanced-keys"   element={<AdvancedKeys />} />
              <Route path="/gamepad"         element={<Gamepad />} />
              <Route path="/calibration"     element={<Calibration />} />
              <Route path="/lighting"        element={<Lighting />} />
              <Route path="/rotary"          element={<Rotary />} />
              <Route path="/device"          element={<Device />} />
              {developerMode && (
                <Route path="/diagnostics"   element={<Diagnostics />} />
              )}
              {/* Legacy redirects for old routes */}
              <Route path="/remap"           element={<Keymap />} />
              <Route path="/led"             element={<Lighting />} />
              <Route path="/settings"        element={<Device />} />
            </Routes>
          </Suspense>
        </main>
      </SidebarInset>
    </SidebarProvider>
  );
}
