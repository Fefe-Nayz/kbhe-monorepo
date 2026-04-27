import type { ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { kbheDevice } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { queryKeys } from "@/lib/query/keys";
import { useDashboardMcuTrendStore } from "@/stores/dashboard-mcu-trends-store";
import { SectionCard } from "@/components/shared/SectionCard";
import { PageContent } from "@/components/shared/PageLayout";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { Sparkline } from "@/components/ui/sparkline";
import { LED_EFFECT_NAMES } from "@/lib/kbhe/protocol";
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
} from "@tabler/icons-react";

const POLL_INTERVAL = 5000;

interface QuickLinkItem {
  icon: typeof IconKeyboard;
  title: string;
  path: string;
  description: string;
}

const BASE_QUICK_LINKS: QuickLinkItem[] = [
  { icon: IconKeyboard, title: "Keymap", path: "/keymap", description: "Remap keys" },
  { icon: IconBrandSpeedtest, title: "Performance", path: "/performance", description: "Actuation & RT" },
  { icon: IconBulb, title: "Lighting", path: "/lighting", description: "Effects & Matrix" },
  { icon: IconDeviceGamepad2, title: "Gamepad", path: "/gamepad", description: "Controller setup" },
  { icon: IconUpload, title: "Firmware", path: "/firmware", description: "Update firmware" },
  { icon: IconSettings, title: "Device", path: "/device", description: "Settings" },
  { icon: IconCpu, title: "Calibration", path: "/calibration", description: "Sensor tuning" },
];

const DIAGNOSTICS_LINK: QuickLinkItem = {
  icon: IconActivity,
  title: "Diagnostics",
  path: "/diagnostics",
  description: "Debug tools",
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

function OverviewPill({
  label,
  value,
  icon: Icon,
}: {
  label: string;
  value: ReactNode;
  icon: typeof IconKeyboard;
}) {
  return (
    <div className="rounded-lg border bg-background/60 px-3 py-2">
      <div className="mb-1 flex items-center gap-2 text-muted-foreground">
        <Icon className="size-3.5" />
        <span className="text-xs">{label}</span>
      </div>
      <div className="text-sm font-medium truncate">{value}</div>
    </div>
  );
}

function MetricTrendCard({
  label,
  value,
  values,
  accentClassName,
}: {
  label: string;
  value: string;
  values: number[];
  accentClassName?: string;
}) {
  return (
    <div className="rounded-lg border bg-background/60 px-3 py-3">
      <div className="flex items-center justify-between gap-3">
        <div className="min-w-0">
          <p className="text-xs text-muted-foreground">{label}</p>
          <p className="font-mono text-sm font-medium truncate">{value}</p>
        </div>
        <Sparkline
          values={values}
          className="h-8 w-28"
          colorClassName={accentClassName ?? "text-primary/70"}
        />
      </div>
    </div>
  );
}

function QuickLink({ icon: Icon, title, path, description }: {
  icon: typeof IconKeyboard; title: string; path: string; description: string;
}) {
  const navigate = useNavigate();
  return (
    <Button
      variant="outline"
      className="h-auto min-h-20 p-4 flex-col items-start gap-1 border-border/70 hover:bg-muted/60"
      onClick={() => navigate(path)}
    >
      <div className="flex items-center gap-2">
        <Icon className="size-4 text-muted-foreground" />
        <span className="font-medium text-sm">{title}</span>
      </div>
      <span className="text-xs text-muted-foreground">{description}</span>
    </Button>
  );
}

export default function Dashboard() {
  const { status, firmwareVersion } = useDeviceSession();
  const developerMode = useDeviceSession((state) => state.developerMode);
  const { gamepad, nkro, ledEffect, mcu, connected } = useDeviceOverview();
  const mcuTrends = useDashboardMcuTrendStore((state) => state.trends);

  const quickLinks = developerMode ? [...BASE_QUICK_LINKS, DIAGNOSTICS_LINK] : BASE_QUICK_LINKS;
  const ledEffectLabel = ledEffect != null
    ? (LED_EFFECT_NAMES[ledEffect as number] ?? `Effect ${ledEffect}`)
    : "Unknown";

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent containerClassName="max-w-5xl">
        <SectionCard className="border-border/70 bg-gradient-to-br from-card via-card to-muted/40">
          <div className="flex flex-col gap-4 md:flex-row md:items-start md:justify-between">
            <div className="space-y-1">
              <p className="text-xs uppercase tracking-wide text-muted-foreground">Device Dashboard</p>
              <h2 className="text-lg font-semibold tracking-tight">Keyboard overview and live telemetry</h2>
              <p className="text-sm text-muted-foreground">
                Monitor connection state, active modes, and MCU activity in real time.
              </p>
            </div>
            <Badge variant={connected ? "default" : "secondary"} className="w-fit capitalize">
              {status}
            </Badge>
          </div>

          <div className="mt-4 grid gap-2 sm:grid-cols-2 lg:grid-cols-4">
            <OverviewPill
              label="Firmware"
              icon={IconUpload}
              value={firmwareVersion ? <span className="font-mono">{firmwareVersion}</span> : <Skeleton className="h-4 w-20" />}
            />
            <OverviewPill
              label="Current LED Effect"
              icon={IconBulb}
              value={ledEffectLabel}
            />
            <OverviewPill
              label="Input Modes"
              icon={IconKeyboard}
              value={
                <span className="flex items-center gap-1.5">
                  <Badge variant="secondary" className="text-[10px]">KB</Badge>
                  {gamepad && <Badge variant="secondary" className="text-[10px]">GP</Badge>}
                  {nkro && <Badge variant="secondary" className="text-[10px]">NKRO</Badge>}
                </span>
              }
            />
            <OverviewPill
              label="Connection"
              icon={IconActivity}
              value={connected ? "Active" : "Waiting for device"}
            />
          </div>
        </SectionCard>

        {mcu && (
          <SectionCard title="MCU Live Metrics" description="Updated every 5 seconds.">
            <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4">
              <MetricTrendCard
                label="Temperature"
                value={mcu.temperature_valid && mcu.temperature_c != null ? `${mcu.temperature_c.toFixed(1)} deg C` : "—"}
                values={mcuTrends.temperature}
                accentClassName="text-orange-500/80"
              />
              <MetricTrendCard
                label="Vref"
                value={`${(mcu.vref_mv / 1000).toFixed(3)} V`}
                values={mcuTrends.vref}
                accentClassName="text-cyan-500/80"
              />
              <MetricTrendCard
                label="Scan Rate"
                value={`${mcu.scan_rate_hz} Hz`}
                values={mcuTrends.scanRate}
                accentClassName="text-emerald-500/80"
              />
              <MetricTrendCard
                label="CPU Load"
                value={`${mcu.load_percent.toFixed(1)}%`}
                values={mcuTrends.load}
                accentClassName="text-violet-500/80"
              />
            </div>
          </SectionCard>
        )}

        {!mcu && connected && (
          <SectionCard title="MCU Live Metrics" description="Telemetry will appear after first sample.">
            <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4">
              <Skeleton className="h-[62px]" />
              <Skeleton className="h-[62px]" />
              <Skeleton className="h-[62px]" />
              <Skeleton className="h-[62px]" />
            </div>
          </SectionCard>
        )}

        <SectionCard title="Quick Links" description="Fast access to the most-used configuration pages.">
          <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
            {quickLinks.map((link) => (
              <QuickLink
                key={link.path}
                icon={link.icon}
                title={link.title}
                path={link.path}
                description={link.description}
              />
            ))}
          </div>
        </SectionCard>
      </PageContent>
    </div>
  );
}
