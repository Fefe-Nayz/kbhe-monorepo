import { useQuery } from "@tanstack/react-query";
import { kbheDevice } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { queryKeys } from "@/lib/query/keys";
import { SectionCard } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
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
    queryKey: ["device", "mcu"],
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected,
    refetchInterval: POLL_INTERVAL,
  });

  return { gamepad: gamepadQ.data, nkro: nkroQ.data, ledEffect: ledEffectQ.data, mcu: mcuQ.data, connected };
}

function QuickLink({ icon: Icon, title, path, description }: {
  icon: typeof IconKeyboard; title: string; path: string; description: string;
}) {
  const navigate = useNavigate();
  return (
    <Button variant="outline" className="h-auto p-4 flex-col items-start gap-1"
      onClick={() => navigate(path)}>
      <div className="flex items-center gap-2">
        <Icon className="size-4 text-muted-foreground" />
        <span className="font-medium text-sm">{title}</span>
      </div>
      <span className="text-xs text-muted-foreground">{description}</span>
    </Button>
  );
}

export default function Dashboard() {
  const { status, firmwareVersion, deviceInfo } = useDeviceSession();
  const { gamepad, nkro, ledEffect, mcu, connected } = useDeviceOverview();

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2">
        <PageHeader title="Dashboard" description={deviceInfo?.product ?? "KBHE Configurator"} />
      </div>

      <div className="flex-1 overflow-y-auto p-4">
        <div className="flex flex-col gap-4 max-w-4xl mx-auto">

          <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
            <SectionCard>
              <div className="flex flex-col gap-1">
                <span className="text-xs text-muted-foreground">Status</span>
                <Badge variant={connected ? "default" : "secondary"} className="w-fit">{status}</Badge>
              </div>
            </SectionCard>
            <SectionCard>
              <div className="flex flex-col gap-1">
                <span className="text-xs text-muted-foreground">Firmware</span>
                {firmwareVersion ? (
                  <span className="text-sm font-mono">{firmwareVersion}</span>
                ) : (
                  <Skeleton className="h-4 w-16" />
                )}
              </div>
            </SectionCard>
            <SectionCard>
              <div className="flex flex-col gap-1">
                <span className="text-xs text-muted-foreground">LED Effect</span>
                {ledEffect != null ? (
                  <span className="text-sm">{LED_EFFECT_NAMES[ledEffect as number] ?? `Effect ${ledEffect}`}</span>
                ) : (
                  <Skeleton className="h-4 w-20" />
                )}
              </div>
            </SectionCard>
            <SectionCard>
              <div className="flex flex-col gap-1">
                <span className="text-xs text-muted-foreground">Modes</span>
                <div className="flex flex-wrap gap-1">
                  <Badge variant="secondary" className="text-xs">
                    <IconKeyboard className="size-3 mr-1" />KB
                  </Badge>
                  {gamepad && (
                    <Badge variant="secondary" className="text-xs">
                      <IconDeviceGamepad2 className="size-3 mr-1" />GP
                    </Badge>
                  )}
                  {nkro && <Badge variant="secondary" className="text-xs">NKRO</Badge>}
                </div>
              </div>
            </SectionCard>
          </div>

          {mcu && (
            <SectionCard title="MCU Metrics">
              <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                <div className="flex flex-col gap-1">
                  <span className="text-xs text-muted-foreground">Temperature</span>
                  <span className="text-sm font-mono">{mcu.temperature_c?.toFixed(1) ?? "—"} °C</span>
                </div>
                <div className="flex flex-col gap-1">
                  <span className="text-xs text-muted-foreground">Vref</span>
                  <span className="text-sm font-mono">{(mcu.vref_mv / 1000).toFixed(3)} V</span>
                </div>
                <div className="flex flex-col gap-1">
                  <span className="text-xs text-muted-foreground">Scan Rate</span>
                  <span className="text-sm font-mono">{mcu.scan_rate_hz} Hz</span>
                </div>
                <div className="flex flex-col gap-1">
                  <span className="text-xs text-muted-foreground">CPU Load</span>
                  <span className="text-sm font-mono">{(mcu.load_percent).toFixed(1)}%</span>
                </div>
              </div>
            </SectionCard>
          )}

          <SectionCard title="Quick Links">
            <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
              <QuickLink icon={IconKeyboard} title="Keymap" path="/keymap" description="Remap keys" />
              <QuickLink icon={IconBrandSpeedtest} title="Performance" path="/performance" description="Actuation & RT" />
              <QuickLink icon={IconBulb} title="Lighting" path="/lighting" description="Effects & Matrix" />
              <QuickLink icon={IconDeviceGamepad2} title="Gamepad" path="/gamepad" description="Controller setup" />
              <QuickLink icon={IconUpload} title="Firmware" path="/firmware" description="Update firmware" />
              <QuickLink icon={IconSettings} title="Device" path="/device" description="Settings" />
              <QuickLink icon={IconActivity} title="Diagnostics" path="/diagnostics" description="Debug tools" />
              <QuickLink icon={IconCpu} title="Calibration" path="/calibration" description="Sensor tuning" />
            </div>
          </SectionCard>
        </div>
      </div>
    </div>
  );
}
