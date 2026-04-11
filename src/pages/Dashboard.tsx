import { useQuery } from "@tanstack/react-query";
import { kbheDevice } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { queryKeys } from "@/lib/query/keys";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageLayout, PageHeader } from "@/components/shared/PageLayout";
import { LiveStatusChip } from "@/components/shared/LiveStatusChip";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import {
  IconKeyboard,
  IconDeviceGamepad2,
  IconBulb,
} from "@tabler/icons-react";
import { LED_EFFECT_NAMES } from "@/lib/kbhe/protocol";

const POLL_INTERVAL = 5000;

function useDeviceOverview() {
  const { status } = useDeviceSession();
  const enabled = status === "connected";

  const options = useQuery({
    queryKey: queryKeys.device.options(),
    queryFn: () => kbheDevice.getOptions(),
    enabled,
    refetchInterval: POLL_INTERVAL,
  });

  const nkro = useQuery({
    queryKey: queryKeys.device.nkroEnabled(),
    queryFn: () => kbheDevice.getNkroEnabled(),
    enabled,
    refetchInterval: POLL_INTERVAL,
  });

  const effect = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled,
    refetchInterval: POLL_INTERVAL,
  });

  const metrics = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled,
    refetchInterval: POLL_INTERVAL,
  });

  return { options, nkro, effect, metrics };
}

function MetricBadge({ label, value }: { label: string; value: string | null }) {
  return (
    <div className="flex flex-col gap-0.5">
      <span className="text-xs text-muted-foreground">{label}</span>
      {value == null ? (
        <Skeleton className="h-5 w-16" />
      ) : (
        <span className="text-sm font-medium tabular-nums">{value}</span>
      )}
    </div>
  );
}

export default function Dashboard() {
  const { status, firmwareVersion, deviceInfo } = useDeviceSession();
  const { options, nkro, effect, metrics } = useDeviceOverview();

  const connected = status === "connected";

  const kbEnabled  = options.data?.keyboard_enabled ?? null;
  const gpEnabled  = options.data?.gamepad_enabled  ?? null;
  const nkroOn     = nkro.data ?? null;
  const effectMode = effect.data ?? null;
  const mcuData    = metrics.data ?? null;

  return (
    <PageLayout>
      <PageHeader
        title="Dashboard"
        description="Live status of your connected KBHE device"
      />

      {/* Connection status */}
      <SectionCard
        title="Connection"
        headerRight={
          connected ? (
            <LiveStatusChip label="Connected" variant="green" />
          ) : status === "connecting" ? (
            <LiveStatusChip label="Connecting…" variant="yellow" />
          ) : (
            <LiveStatusChip label="Disconnected" variant="red" />
          )
        }
      >
        <div className="grid grid-cols-2 gap-4 sm:grid-cols-3">
          <MetricBadge label="Device" value={deviceInfo?.product ?? "—"} />
          <MetricBadge label="Firmware" value={firmwareVersion ?? "—"} />
          <MetricBadge
            label="Serial"
            value={deviceInfo?.serialNumber ?? "—"}
          />
        </div>
      </SectionCard>

      {/* HID modes */}
      <SectionCard title="HID Modes">
        <div className="flex flex-wrap gap-2">
          {kbEnabled == null ? (
            <Skeleton className="h-6 w-28" />
          ) : (
            <Badge variant={kbEnabled ? "default" : "secondary"}>
              <IconKeyboard className="size-3 mr-1" />
              Keyboard {kbEnabled ? "ON" : "OFF"}
            </Badge>
          )}
          {gpEnabled == null ? (
            <Skeleton className="h-6 w-28" />
          ) : (
            <Badge variant={gpEnabled ? "default" : "secondary"}>
              <IconDeviceGamepad2 className="size-3 mr-1" />
              Gamepad {gpEnabled ? "ON" : "OFF"}
            </Badge>
          )}
          {nkroOn == null ? (
            <Skeleton className="h-6 w-16" />
          ) : (
            <Badge variant="outline">
              {nkroOn ? "NKRO" : "6KRO"}
            </Badge>
          )}
        </div>
      </SectionCard>

      {/* LED */}
      <SectionCard title="Lighting">
        <FormRow label="Active effect">
          {effectMode == null ? (
            <Skeleton className="h-5 w-28" />
          ) : (
            <Badge variant="outline">
              <IconBulb className="size-3 mr-1" />
              {LED_EFFECT_NAMES[effectMode] ?? `Effect ${effectMode}`}
            </Badge>
          )}
        </FormRow>
      </SectionCard>

      {/* MCU metrics */}
      {connected && (
        <SectionCard title="MCU Metrics">
          <div className="grid grid-cols-2 gap-4 sm:grid-cols-4">
            <MetricBadge
              label="Scan rate"
              value={mcuData ? `${mcuData.scan_rate_hz.toFixed(0)} Hz` : null}
            />
            <MetricBadge
              label="CPU load"
              value={mcuData ? `${mcuData.load_percent.toFixed(1)}%` : null}
            />
            <MetricBadge
              label="Core clock"
              value={
                mcuData
                  ? `${(mcuData.core_clock_hz / 1_000_000).toFixed(0)} MHz`
                  : null
              }
            />
            <MetricBadge
              label="Temperature"
              value={
                mcuData && mcuData.temperature_valid
                  ? `${mcuData.temperature_c?.toFixed(1)}°C`
                  : mcuData
                    ? "N/A"
                    : null
              }
            />
          </div>
        </SectionCard>
      )}
    </PageLayout>
  );
}
