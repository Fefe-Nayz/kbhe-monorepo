import { useQuery } from "@tanstack/react-query";
import { isTauri } from "@tauri-apps/api/core";
import { useNavigate } from "react-router-dom";
import { Button } from "@/components/ui/button";
import { checkAppUpdate, checkFirmwareUpdate } from "@/lib/kbhe/releases";
import { useDeviceSession } from "@/lib/kbhe/session";
import { IconDownload, IconRefresh } from "@tabler/icons-react";
import { cn } from "@/lib/utils";

const APP_RELEASE_QUERY_KEY = ["app", "release"] as const;
const APP_RELEASE_REFETCH_INTERVAL_MS = 60 * 60 * 1000;
const FIRMWARE_RELEASE_REFETCH_INTERVAL_MS = 30 * 60 * 1000;
const UPDATE_STALE_TIME_MS = 10 * 60 * 1000;

function updateLabel(tag?: string | null, version?: string | null): string {
  return tag ?? version ?? "latest";
}

export function UpdateBanner() {
  const navigate = useNavigate();
  const firmwareVersion = useDeviceSession((state) => state.firmwareVersion);

  const appUpdateQ = useQuery({
    queryKey: APP_RELEASE_QUERY_KEY,
    queryFn: checkAppUpdate,
    enabled: isTauri(),
    refetchInterval: APP_RELEASE_REFETCH_INTERVAL_MS,
    staleTime: UPDATE_STALE_TIME_MS,
  });

  const firmwareUpdateQ = useQuery({
    queryKey: ["release", "firmware", firmwareVersion],
    queryFn: () => checkFirmwareUpdate(firmwareVersion),
    enabled: isTauri() && Boolean(firmwareVersion),
    refetchInterval: FIRMWARE_RELEASE_REFETCH_INTERVAL_MS,
    staleTime: UPDATE_STALE_TIME_MS,
  });

  if (!isTauri()) {
    return null;
  }

  const appUpdate = appUpdateQ.data?.updateAvailable ? appUpdateQ.data : null;
  const firmwareUpdate = firmwareUpdateQ.data?.updateAvailable ? firmwareUpdateQ.data : null;

  if (!appUpdate && !firmwareUpdate) {
    return null;
  }

  const appLabel = appUpdate ? updateLabel(appUpdate.tag, appUpdate.version) : null;
  const firmwareLabel = firmwareUpdate
    ? updateLabel(firmwareUpdate.tag, firmwareUpdate.version)
    : null;
  const refreshBusy = appUpdateQ.isFetching || firmwareUpdateQ.isFetching;

  const description = appUpdate && firmwareUpdate
    ? `App ${appLabel} and firmware ${firmwareLabel} are available.`
    : appUpdate
      ? `App update ${appLabel} is available.`
      : `Firmware update ${firmwareLabel} is available.`;

  return (
    <div className="flex min-h-11 items-center gap-3 border-b border-warning/30 bg-warning/10 px-4 py-2 text-sm text-warning">
      <div className="flex min-w-0 flex-1 items-center gap-2">
        <IconDownload className="size-4 shrink-0" />
        <span className="truncate">{description}</span>
      </div>

      <div className="flex shrink-0 items-center gap-2">
        {appUpdate && (
          <Button
            variant="outline"
            size="sm"
            className="h-7 border-warning/40 bg-transparent text-xs text-warning hover:bg-warning/15 dark:text-warning"
            onClick={() => navigate("/settings")}
          >
            Open App Updates
          </Button>
        )}

        {firmwareUpdate && (
          <Button
            variant="outline"
            size="sm"
            className="h-7 border-warning/40 bg-transparent text-xs text-warning hover:bg-warning/15 dark:text-warning"
            onClick={() => navigate("/firmware")}
          >
            Open Firmware Updates
          </Button>
        )}

        <Button
          variant="ghost"
          size="sm"
          className="h-7 gap-1.5 px-2 text-xs text-warning hover:bg-warning/15 dark:text-warning"
          disabled={refreshBusy}
          onClick={() => {
            void appUpdateQ.refetch();
            if (firmwareVersion) {
              void firmwareUpdateQ.refetch();
            }
          }}
        >
          <IconRefresh className={cn("size-3", refreshBusy && "animate-spin")} />
          Refresh
        </Button>
      </div>
    </div>
  );
}