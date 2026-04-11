import { useDeviceSession } from "@/lib/kbhe/session";
import { DeviceSessionManager } from "@/lib/kbhe/session";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  IconPlugConnected,
  IconPlugConnectedX,
  IconLoader2,
  IconAlertTriangle,
  IconRefresh,
} from "@tabler/icons-react";
import { cn } from "@/lib/utils";

const STATUS_CONFIG = {
  disconnected: {
    label: "No device",
    icon: IconPlugConnectedX,
    variant: "secondary" as const,
    className: "text-muted-foreground",
  },
  connecting: {
    label: "Connecting…",
    icon: IconLoader2,
    variant: "secondary" as const,
    className: "text-muted-foreground animate-spin",
  },
  connected: {
    label: "Connected",
    icon: IconPlugConnected,
    variant: "default" as const,
    className: "text-green-500",
  },
  updater: {
    label: "Updater mode",
    icon: IconPlugConnected,
    variant: "default" as const,
    className: "text-yellow-500",
  },
  error: {
    label: "Connection error",
    icon: IconAlertTriangle,
    variant: "destructive" as const,
    className: "text-red-500",
  },
} as const;

/** Inline chip shown in the header — tiny footprint, always visible */
export function DeviceStatusChip() {
  const { status } = useDeviceSession();
  const cfg = STATUS_CONFIG[status];
  const Icon = cfg.icon;

  return (
    <Badge variant={cfg.variant} className="gap-1.5 text-xs font-medium">
      <Icon className={cn("size-3", status === "connecting" && "animate-spin")} />
      {cfg.label}
    </Badge>
  );
}

/** Full-width banner shown only when disconnected or errored */
export function DeviceBanner() {
  const { status, error, firmwareVersion } = useDeviceSession();

  if (status === "connected" || status === "updater") return null;

  const isError = status === "error";

  return (
    <div
      className={cn(
        "flex items-center justify-between px-4 py-2 text-sm border-b",
        isError
          ? "bg-destructive/10 border-destructive/30 text-destructive"
          : "bg-muted/60 border-border text-muted-foreground",
      )}
    >
      <div className="flex items-center gap-2">
        {isError ? (
          <IconAlertTriangle className="size-4 shrink-0" />
        ) : (
          <IconPlugConnectedX className="size-4 shrink-0" />
        )}
        <span>
          {isError
            ? `Device error: ${error ?? "unknown"}`
            : status === "connecting"
              ? "Connecting to KBHE device…"
              : "No KBHE device detected. Connect via USB."}
        </span>
      </div>

      {status !== "connecting" && (
        <Button
          variant="ghost"
          size="sm"
          className="h-7 gap-1.5 text-xs"
          onClick={() => void DeviceSessionManager.reconnect()}
        >
          <IconRefresh className="size-3" />
          Retry
        </Button>
      )}

      {firmwareVersion && (
        <span className="text-xs opacity-60">fw {firmwareVersion}</span>
      )}
    </div>
  );
}
