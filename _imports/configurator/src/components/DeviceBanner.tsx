import { useQuery } from "@tanstack/react-query";
import { isTauri } from "@tauri-apps/api/core";
import { useDeviceSession } from "@/lib/kbhe/session";
import { DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheTransport } from "@/lib/kbhe/transport";
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

  const bootloaderPresenceQ = useQuery({
    queryKey: ["firmware", "bootloaderPresence"],
    queryFn: () => kbheTransport.detectBootloaderPresence(),
    enabled: isTauri() && status !== "connected" && status !== "updater",
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const bootloaderDetected = status !== "connected" && status !== "updater" && Boolean(bootloaderPresenceQ.data);

  if (status === "connected" || status === "updater" || bootloaderDetected) return null;

  const isError = status === "error";

  return (
    <div
      className={cn(
        "flex min-h-11 items-center gap-3 border-b px-4 py-2 text-sm",
        isError
          ? "bg-destructive/10 border-destructive/30 text-destructive"
          : "bg-muted/60 border-border text-muted-foreground",
      )}
    >
      <div className="flex min-w-0 flex-1 items-center gap-2">
        {isError ? (
          <IconAlertTriangle className="size-4 shrink-0" />
        ) : (
          <IconPlugConnectedX className="size-4 shrink-0" />
        )}
        <span className="truncate">
          {isError
            ? `Device error: ${error ?? "unknown"}`
            : status === "connecting"
              ? "Connecting to KBHE device…"
              : "No KBHE device detected. Connect via USB."}
        </span>
      </div>

      <div className="flex w-36 shrink-0 items-center justify-end gap-2">
        {firmwareVersion && (
          <span className="truncate text-xs opacity-60">fw {firmwareVersion}</span>
        )}
        <Button
          variant="ghost"
          size="sm"
          className={cn("h-7 gap-1.5 text-xs", status === "connecting" && "invisible pointer-events-none")}
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
