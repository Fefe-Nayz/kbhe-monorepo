import { useQuery } from "@tanstack/react-query";
import { isTauri } from "@tauri-apps/api/core";
import { useDeviceSession } from "@/lib/kbhe/session";
import { DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheTransport } from "@/lib/kbhe/transport";
import { libhmkRgbBridge } from "@/lib/kbhe/rgb-bridge";
import { Button } from "@/components/ui/button";
import {
  IconPlugConnected,
  IconPlugConnectedX,
  IconLoader2,
  IconAlertTriangle,
  IconRefresh,
  IconBulb,
} from "@tabler/icons-react";
import { cn } from "@/lib/utils";

const STATUS_CONFIG = {
  disconnected: {
    label: "No device",
    icon: IconPlugConnectedX,
    dotClassName: "bg-muted-foreground/50",
    className: "border-border bg-muted/60 text-muted-foreground",
  },
  connecting: {
    label: "Connecting",
    icon: IconLoader2,
    dotClassName: "bg-warning",
    className: "border-border bg-muted/60 text-muted-foreground",
  },
  connected: {
    label: "Connected",
    icon: IconPlugConnected,
    dotClassName: "bg-success",
    className: "border-success/30 bg-success/10 text-success",
  },
  updater: {
    label: "Updater",
    icon: IconPlugConnected,
    dotClassName: "bg-warning",
    className: "border-warning/30 bg-warning/10 text-warning",
  },
  "recovery-only": {
    label: "Recovery",
    icon: IconAlertTriangle,
    dotClassName: "bg-warning",
    className: "border-warning/30 bg-warning/10 text-warning",
  },
  error: {
    label: "Error",
    icon: IconAlertTriangle,
    dotClassName: "bg-destructive",
    className: "border-destructive/30 bg-destructive/10 text-destructive",
  },
} as const;

/** Inline chip shown in the header — tiny footprint, always visible */
export function DeviceStatusChip() {
  const { status } = useDeviceSession();
  const cfg = STATUS_CONFIG[status];

  return (
    <span
      className={cn(
        "inline-flex h-7 items-center gap-1.5 rounded-full border px-2.5 text-xs font-medium",
        cfg.className,
      )}
      title={`Device status: ${cfg.label}`}
    >
      <span
        className={cn(
          "size-1.5 rounded-full",
          cfg.dotClassName,
          status === "connecting" && "animate-pulse",
        )}
      />
      {cfg.label}
    </span>
  );
}

/** Full-width banner shown only when disconnected or errored */
export function DeviceBanner() {
  const { status, error, firmwareVersion } = useDeviceSession();

  const bootloaderPresenceQ = useQuery({
    queryKey: ["firmware", "bootloaderPresence"],
    queryFn: () => kbheTransport.detectBootloaderPresence(),
    enabled: isTauri()
      && status !== "connected"
      && status !== "updater"
      && status !== "recovery-only",
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const bootloaderDetected = status !== "connected"
    && status !== "updater"
    && status !== "recovery-only"
    && Boolean(bootloaderPresenceQ.data);

  const rgbBridgePresenceQ = useQuery({
    queryKey: ["libhmk-rgb-bridge", "devices"],
    queryFn: () => libhmkRgbBridge.listDevices(),
    enabled: isTauri()
      && status !== "connected"
      && status !== "updater"
      && status !== "recovery-only",
    refetchInterval: 3000,
    staleTime: 1000,
  });
  const rgbBridgeDevice = rgbBridgePresenceQ.data?.[0];

  if (
    status === "connected"
    || status === "updater"
    || status === "recovery-only"
    || bootloaderDetected
  ) return null;

  if (rgbBridgeDevice) {
    return (
      <div className="flex min-h-10 items-center gap-2.5 border-b border-info/25 bg-info/8 px-5 py-2 text-xs">
        <IconBulb className="size-4 shrink-0 text-info" />
        <span className="truncate text-foreground/90">
          <span className="font-medium">{rgbBridgeDevice.product ?? "libhmk keyboard"}</span>{" "}
          detected in isolated RGB bridge mode — lighting controls are available; native
          profiles and firmware updates stay disabled.
        </span>
      </div>
    );
  }

  const isError = status === "error";

  return (
    <div
      className={cn(
        "flex min-h-10 items-center gap-2.5 border-b px-5 py-2 text-xs",
        isError
          ? "border-destructive/25 bg-destructive/8 text-destructive"
          : "border-border bg-muted/50 text-muted-foreground",
      )}
    >
      <div className="flex min-w-0 flex-1 items-center gap-2">
        {isError ? (
          <IconAlertTriangle className="size-3.5 shrink-0" />
        ) : status === "connecting" ? (
          <IconLoader2 className="size-3.5 shrink-0 animate-spin" />
        ) : (
          <IconPlugConnectedX className="size-3.5 shrink-0" />
        )}
        <span className="truncate">
          {isError
            ? `Device error — ${error ?? "unknown"}`
            : status === "connecting"
              ? "Connecting to KBHE device…"
              : "No KBHE device detected. Connect your keyboard over USB."}
        </span>
      </div>

      <div className="flex shrink-0 items-center gap-2">
        {firmwareVersion && (
          <span className="truncate font-mono text-[0.68rem] opacity-60">fw {firmwareVersion}</span>
        )}
        <Button
          variant="ghost"
          size="xs"
          className={cn(
            "gap-1.5",
            isError && "text-destructive hover:bg-destructive/10 hover:text-destructive",
            status === "connecting" && "invisible pointer-events-none",
          )}
          disabled={status === "connecting"}
          onClick={() => void DeviceSessionManager.reconnect()}
        >
          <IconRefresh className="size-3" />
          Retry
        </Button>
      </div>
    </div>
  );
}
