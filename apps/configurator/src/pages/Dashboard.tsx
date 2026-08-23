import type { ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { kbheDevice } from "@/lib/kbhe/device";
import { DeviceSessionManager, useDeviceSession } from "@/lib/kbhe/session";
import { queryKeys } from "@/lib/query/keys";
import { useDashboardMcuTrendStore } from "@/stores/dashboard-mcu-trends-store";
import { SectionCard } from "@/components/shared/SectionCard";
import { PageContent, PageSection } from "@/components/shared/PageLayout";
import { StatTile } from "@/components/shared/StatTile";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { Sparkline } from "@/components/ui/sparkline";
import { LED_EFFECT_NAMES } from "@/lib/kbhe/protocol";
import { cn } from "@/lib/utils";
import { useNavigate } from "react-router-dom";
import {
  IconKeyboard,
  IconDeviceGamepad2,
  IconBulb,
  IconCpu,
  IconActivity,
  IconBrandSpeedtest,
  IconUpload,
  IconSettings,
  IconChevronRight,
  IconPlugConnectedX,
  IconRefresh,
  IconTemperature,
  IconBolt,
  IconWaveSine,
  IconGauge,
} from "@tabler/icons-react";

const POLL_INTERVAL = 5000;

interface QuickLinkItem {
  icon: typeof IconKeyboard;
  title: string;
  path: string;
  description: string;
}

const BASE_QUICK_LINKS: QuickLinkItem[] = [
  { icon: IconKeyboard, title: "Keymap", path: "/keymap", description: "Remap keys per layer" },
  { icon: IconBrandSpeedtest, title: "Performance", path: "/performance", description: "Actuation & rapid trigger" },
  { icon: IconBulb, title: "Lighting", path: "/lighting", description: "Effects & per-key matrix" },
  { icon: IconDeviceGamepad2, title: "Gamepad", path: "/gamepad", description: "Controller output" },
  { icon: IconUpload, title: "Firmware", path: "/firmware", description: "Flash a new build" },
  { icon: IconSettings, title: "Device", path: "/device", description: "Identity & input modes" },
  { icon: IconCpu, title: "Calibration", path: "/calibration", description: "Sensor range tuning" },
];

const DIAGNOSTICS_LINK: QuickLinkItem = {
  icon: IconActivity,
  title: "Diagnostics",
  path: "/diagnostics",
  description: "Protocol debug tools",
};

function useDeviceOverview() {
  const { status } = useDeviceSession();
  const connected = status === "connected";

  const gamepadQ = useQuery({
    queryKey: queryKeys.device.gamepadEnabled(),
    queryFn: async () => {
      const opts = await kbheDevice.getOptions();
      return opts?.gamepad_enabled ?? false;
    },
    enabled: connected,
    refetchInterval: POLL_INTERVAL,
  });

  const nkroQ = useQuery({
    queryKey: queryKeys.device.nkroEnabled(),
    queryFn: () => kbheDevice.getNkroEnabled(),
    enabled: connected,
    refetchInterval: POLL_INTERVAL,
  });

  const ledEffectQ = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled: connected,
    refetchInterval: POLL_INTERVAL,
  });

  const mcuQ = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected,
    staleTime: POLL_INTERVAL,
  });

  return { gamepad: gamepadQ.data, nkro: nkroQ.data, ledEffect: ledEffectQ.data, mcu: mcuQ.data, connected };
}

function MetricTrendCard({
  label,
  value,
  values,
  icon,
  accentClassName,
}: {
  label: string;
  value: string;
  values: number[];
  icon: ReactNode;
  accentClassName: string;
}) {
  return (
    <div className="rounded-xl border bg-card px-3.5 py-3">
      <div className="flex items-center gap-2 text-muted-foreground">
        <span className={cn("[&_svg]:size-3.5", accentClassName)}>{icon}</span>
        <span className="truncate text-[0.7rem] font-medium uppercase tracking-[0.06em]">
          {label}
        </span>
      </div>
      <div className="mt-2 flex items-end justify-between gap-3">
        <p className="truncate font-mono text-lg font-semibold leading-none">{value}</p>
        <Sparkline values={values} className="h-7 w-24" colorClassName={accentClassName} />
      </div>
    </div>
  );
}

function QuickLink({ icon: Icon, title, path, description }: QuickLinkItem) {
  const navigate = useNavigate();
  return (
    <button
      type="button"
      onClick={() => navigate(path)}
      className="group flex items-center gap-3 rounded-xl border bg-card px-3.5 py-3 text-left transition-colors hover:border-primary/35 hover:bg-primary/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60"
    >
      <span className="flex size-9 shrink-0 items-center justify-center rounded-lg bg-muted text-muted-foreground transition-colors group-hover:bg-primary/12 group-hover:text-primary">
        <Icon className="size-4.5" />
      </span>
      <span className="min-w-0 flex-1">
        <span className="block truncate text-sm font-medium">{title}</span>
        <span className="block truncate text-xs text-muted-foreground">{description}</span>
      </span>
      <IconChevronRight className="size-4 shrink-0 text-muted-foreground/40 transition-transform group-hover:translate-x-0.5 group-hover:text-primary" />
    </button>
  );
}

function HeroBanner({
  connected,
  status,
  keyboardName,
  firmwareVersion,
}: {
  connected: boolean;
  status: string;
  keyboardName: string;
  firmwareVersion: string | null;
}) {
  const connecting = status === "connecting";

  return (
    <div className="relative overflow-hidden rounded-2xl border bg-card">
      <div className="kbhe-grid-fade pointer-events-none absolute inset-0 opacity-60" />
      <div
        className={cn(
          "pointer-events-none absolute -right-24 -top-28 size-72 rounded-full blur-3xl transition-colors",
          connected ? "bg-primary/18" : "bg-muted-foreground/8",
        )}
      />
      <div className="relative flex flex-col gap-5 p-6 lg:flex-row lg:items-center lg:justify-between">
        <div className="flex min-w-0 items-center gap-4">
          <div
            className={cn(
              "flex size-12 shrink-0 items-center justify-center rounded-xl",
              connected
                ? "bg-primary/12 text-primary"
                : "bg-muted text-muted-foreground",
            )}
          >
            <IconKeyboard className="size-6" />
          </div>
          <div className="min-w-0">
            <div className="flex items-center gap-2">
              <h2 className="truncate text-xl font-semibold tracking-tight">{keyboardName}</h2>
              <Badge
                variant={connected ? "default" : "secondary"}
                className={cn("capitalize", connected && "bg-success text-success-foreground")}
              >
                {status}
              </Badge>
            </div>
            <p className="mt-1 text-sm text-muted-foreground">
              {connected
                ? "Every change you make below is written straight to the keyboard."
                : connecting
                  ? "Looking for a KBHE keyboard on USB…"
                  : "Connect your keyboard over USB to configure it."}
            </p>
          </div>
        </div>

        <div className="flex shrink-0 items-center gap-2">
          {firmwareVersion && (
            <span className="rounded-lg border bg-background/60 px-2.5 py-1.5 font-mono text-xs text-muted-foreground">
              fw {firmwareVersion}
            </span>
          )}
          {!connected && (
            <Button
              variant="outline"
              size="sm"
              disabled={connecting}
              onClick={() => void DeviceSessionManager.reconnect()}
            >
              <IconRefresh className={connecting ? "animate-spin" : undefined} />
              {connecting ? "Searching…" : "Retry connection"}
            </Button>
          )}
        </div>
      </div>
    </div>
  );
}

export default function Dashboard() {
  const { status, firmwareVersion, deviceInfo } = useDeviceSession();
  const developerMode = useDeviceSession((state) => state.developerMode);
  const { gamepad, nkro, ledEffect, mcu, connected } = useDeviceOverview();
  const mcuTrends = useDashboardMcuTrendStore((state) => state.trends);

  const quickLinks = developerMode ? [...BASE_QUICK_LINKS, DIAGNOSTICS_LINK] : BASE_QUICK_LINKS;
  const ledEffectLabel = ledEffect != null
    ? (LED_EFFECT_NAMES[ledEffect as number] ?? `Effect ${ledEffect}`)
    : "—";
  const keyboardName = connected
    ? (deviceInfo?.product?.trim() || "KBHE Keyboard")
    : "No keyboard connected";

  return (
    <PageContent containerClassName="max-w-6xl">
      <HeroBanner
        connected={connected}
        status={status}
        keyboardName={keyboardName}
        firmwareVersion={firmwareVersion}
      />

      <PageSection title="At a glance">
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <StatTile
            label="Firmware"
            icon={<IconUpload />}
            tone={connected ? "primary" : "default"}
            mono
            value={
              firmwareVersion ?? (connected ? <Skeleton className="h-4 w-16" /> : "—")
            }
            hint={connected ? "Running on device" : "Unavailable offline"}
          />
          <StatTile
            label="LED effect"
            icon={<IconBulb />}
            tone={connected ? "warning" : "default"}
            value={ledEffectLabel}
            hint={connected ? "Active on all keys" : "Unavailable offline"}
          />
          <StatTile
            label="Input modes"
            icon={<IconKeyboard />}
            tone={connected ? "info" : "default"}
            value={
              connected ? (
                <span className="flex items-center gap-1">
                  <Badge variant="secondary" className="text-[0.65rem]">KB</Badge>
                  {gamepad ? <Badge variant="secondary" className="text-[0.65rem]">GP</Badge> : null}
                  {nkro ? <Badge variant="secondary" className="text-[0.65rem]">NKRO</Badge> : null}
                </span>
              ) : (
                "—"
              )
            }
            hint={connected ? "Reported by firmware" : "Unavailable offline"}
          />
          <StatTile
            label="Connection"
            icon={connected ? <IconActivity /> : <IconPlugConnectedX />}
            tone={connected ? "success" : "default"}
            value={connected ? "Active" : "Waiting for device"}
            hint={connected ? "USB HID link established" : "Plug in over USB"}
          />
        </div>
      </PageSection>

      {connected && (
        <PageSection
          title="MCU telemetry"
          description="Sampled every 5 seconds while the keyboard is attached."
        >
          {mcu ? (
            <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
              <MetricTrendCard
                label="Temperature"
                icon={<IconTemperature />}
                value={
                  mcu.temperature_valid && mcu.temperature_c != null
                    ? `${mcu.temperature_c.toFixed(1)} °C`
                    : "—"
                }
                values={mcuTrends.temperature}
                accentClassName="text-chart-4"
              />
              <MetricTrendCard
                label="Vref"
                icon={<IconBolt />}
                value={`${(mcu.vref_mv / 1000).toFixed(3)} V`}
                values={mcuTrends.vref}
                accentClassName="text-chart-2"
              />
              <MetricTrendCard
                label="Scan rate"
                icon={<IconWaveSine />}
                value={`${mcu.scan_rate_hz} Hz`}
                values={mcuTrends.scanRate}
                accentClassName="text-chart-3"
              />
              <MetricTrendCard
                label="CPU load"
                icon={<IconGauge />}
                value={`${mcu.load_percent.toFixed(1)} %`}
                values={mcuTrends.load}
                accentClassName="text-chart-1"
              />
            </div>
          ) : (
            <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
              <Skeleton className="h-[86px] rounded-xl" />
              <Skeleton className="h-[86px] rounded-xl" />
              <Skeleton className="h-[86px] rounded-xl" />
              <Skeleton className="h-[86px] rounded-xl" />
            </div>
          )}
        </PageSection>
      )}

      <PageSection title="Jump to">
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
          {quickLinks.map((link) => (
            <QuickLink key={link.path} {...link} />
          ))}
        </div>
      </PageSection>

      {!connected && (
        <SectionCard
          tone="muted"
          title="Working without a keyboard"
          description="Pages that only need local data stay usable while you are disconnected."
          icon={<IconPlugConnectedX />}
        >
          <p className="text-xs leading-relaxed text-muted-foreground">
            App profiles, appearance settings and firmware files can all be prepared offline.
            Anything that writes to the keyboard stays disabled until a device is detected.
          </p>
        </SectionCard>
      )}
    </PageContent>
  );
}
