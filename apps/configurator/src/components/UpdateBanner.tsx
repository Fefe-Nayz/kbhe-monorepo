import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { isTauri } from "@tauri-apps/api/core";
import { useNavigate } from "react-router-dom";
import { Button } from "@/components/ui/button";
import { checkAppUpdate, checkFirmwareUpdate } from "@/lib/kbhe/releases";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import {
  CALIBRATION_MIGRATION_QUERY_KEY,
  isResetCalibration,
  listCalibrationMigrationBackups,
  restoreCalibrationMigrationBackup,
} from "@/lib/kbhe/calibration-migration";
import { IconAlertTriangle, IconDownload, IconRefresh } from "@tabler/icons-react";
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
  const queryClient = useQueryClient();
  const firmwareVersion = useDeviceSession((state) => state.firmwareVersion);
  const updaterProtocol = useDeviceSession((state) => state.compatibility?.updaterProtocol ?? null);
  const status = useDeviceSession((state) => state.status);
  const deviceInfo = useDeviceSession((state) => state.deviceInfo);
  const serialNumber = deviceInfo?.serialNumber?.trim() || null;
  const runtimeConnected = deviceInfo?.kind === "runtime"
    && (status === "connected" || status === "recovery-only");

  const calibrationRecoveryQ = useQuery({
    queryKey: CALIBRATION_MIGRATION_QUERY_KEY,
    queryFn: () => listCalibrationMigrationBackups(),
    enabled: isTauri(),
    refetchInterval: 3_000,
    staleTime: 1_000,
  });
  const currentCalibrationBackup = calibrationRecoveryQ.data?.find(
    (backup) => backup.serialNumber === serialNumber,
  ) ?? null;
  const pendingCalibrationBackup = currentCalibrationBackup
    ?? calibrationRecoveryQ.data?.[0]
    ?? null;

  const calibrationResetQ = useQuery({
    queryKey: ["calibration", "postMigrationDiagnostic", serialNumber],
    queryFn: () => kbheDevice.getCalibration(),
    enabled: isTauri()
      && runtimeConnected
      && Boolean(serialNumber)
      && calibrationRecoveryQ.isSuccess
      && !currentCalibrationBackup,
    staleTime: 5_000,
  });

  const calibrationRestoreM = useMutation({
    mutationFn: async () => {
      if (!serialNumber || !currentCalibrationBackup || !runtimeConnected) {
        throw new Error("Reconnect the matching keyboard in runtime mode before restoring calibration.");
      }
      const restored = await restoreCalibrationMigrationBackup(serialNumber);
      if (!restored) throw new Error("The calibration backup is no longer available.");
    },
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: CALIBRATION_MIGRATION_QUERY_KEY });
      await queryClient.invalidateQueries({ queryKey: ["calibration"] });
    },
  });

  const appUpdateQ = useQuery({
    queryKey: APP_RELEASE_QUERY_KEY,
    queryFn: checkAppUpdate,
    enabled: isTauri(),
    refetchInterval: APP_RELEASE_REFETCH_INTERVAL_MS,
    staleTime: UPDATE_STALE_TIME_MS,
  });

  const firmwareUpdateQ = useQuery({
    queryKey: ["release", "firmware", firmwareVersion, updaterProtocol],
    queryFn: () => checkFirmwareUpdate(firmwareVersion, updaterProtocol),
    enabled: isTauri() && (Boolean(firmwareVersion) || updaterProtocol != null),
    refetchInterval: FIRMWARE_RELEASE_REFETCH_INTERVAL_MS,
    staleTime: UPDATE_STALE_TIME_MS,
  });

  if (!isTauri()) {
    return null;
  }

  const calibrationRecoveryError = calibrationRestoreM.error ?? calibrationRecoveryQ.error;
  const resetCalibrationDetected = calibrationResetQ.data
    ? isResetCalibration(calibrationResetQ.data)
    : false;

  if (pendingCalibrationBackup || calibrationRecoveryError || resetCalibrationDetected) {
    const description = calibrationRecoveryError
      ? `Calibration recovery needs attention: ${calibrationRecoveryError instanceof Error ? calibrationRecoveryError.message : String(calibrationRecoveryError)}`
      : pendingCalibrationBackup
        ? currentCalibrationBackup && runtimeConnected
          ? "A complete pre-migration calibration backup is waiting for this keyboard. Restore and verify it before typing or flashing again."
          : `A calibration backup for keyboard ${pendingCalibrationBackup.serialNumber} is safe. Reconnect that keyboard in runtime mode to restore it.`
        : "Calibration reset detected (all 82 keys are at 2195/2850). Run guided calibration before normal use.";

    return (
      <div
        className="flex min-h-11 items-center gap-3 border-b border-destructive/30 bg-destructive/10 px-4 py-2 text-sm text-destructive"
        role="alert"
      >
        <div className="flex min-w-0 flex-1 items-center gap-2">
          <IconAlertTriangle className="size-4 shrink-0" />
          <span className="truncate">{description}</span>
        </div>
        <div className="flex shrink-0 items-center gap-2">
          {currentCalibrationBackup && runtimeConnected ? (
            <Button
              variant="outline"
              size="sm"
              className="h-7 border-destructive/40 bg-transparent text-xs text-destructive hover:bg-destructive/15"
              disabled={calibrationRestoreM.isPending}
              onClick={() => calibrationRestoreM.mutate()}
            >
              {calibrationRestoreM.isPending ? "Restoring…" : "Restore calibration"}
            </Button>
          ) : resetCalibrationDetected ? (
            <Button
              variant="outline"
              size="sm"
              className="h-7 border-destructive/40 bg-transparent text-xs text-destructive hover:bg-destructive/15"
              onClick={() => navigate("/calibration")}
            >
              Open Calibration
            </Button>
          ) : (
            <Button
              variant="outline"
              size="sm"
              className="h-7 border-destructive/40 bg-transparent text-xs text-destructive hover:bg-destructive/15"
              onClick={() => navigate("/firmware")}
            >
              Open Firmware Recovery
            </Button>
          )}
        </div>
      </div>
    );
  }

  const appUpdate = appUpdateQ.data?.updateAvailable ? appUpdateQ.data : null;
  const firmwareUpdate = firmwareUpdateQ.data?.updateAvailable ? firmwareUpdateQ.data : null;
  const firmwareBlocked = firmwareUpdateQ.data?.blockedReason ? firmwareUpdateQ.data : null;

  if (!appUpdate && !firmwareUpdate && !firmwareBlocked) {
    return null;
  }

  const appLabel = appUpdate ? updateLabel(appUpdate.tag, appUpdate.version) : null;
  const firmwareLabel = firmwareUpdate
    ? updateLabel(firmwareUpdate.tag, firmwareUpdate.version)
    : null;
  const refreshBusy = appUpdateQ.isFetching || firmwareUpdateQ.isFetching;

  const description = firmwareBlocked
    ? firmwareBlocked.blockedReason
    : appUpdate && firmwareUpdate
    ? `App ${appLabel} and firmware ${firmwareLabel} are available.`
    : appUpdate
      ? `App update ${appLabel} is available.`
      : `Firmware update ${firmwareLabel} is available.`;

  return (
    <div
      className="flex min-h-11 items-center gap-3 border-b border-warning/30 bg-warning/10 px-4 py-2 text-sm text-warning"
      role={firmwareBlocked ? "alert" : undefined}
    >
      <div className="flex min-w-0 flex-1 items-center gap-2">
        {firmwareBlocked
          ? <IconAlertTriangle className="size-4 shrink-0" />
          : <IconDownload className="size-4 shrink-0" />}
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

        {firmwareBlocked && updaterProtocol == null && firmwareVersion && (
          <Button
            variant="outline"
            size="sm"
            className="h-7 border-warning/40 bg-transparent text-xs text-warning hover:bg-warning/15 dark:text-warning"
            onClick={() => navigate("/device")}
          >
            Verify Updater
          </Button>
        )}

        {(firmwareUpdate || firmwareBlocked) && (
          <Button
            variant="outline"
            size="sm"
            className="h-7 border-warning/40 bg-transparent text-xs text-warning hover:bg-warning/15 dark:text-warning"
            onClick={() => navigate("/firmware")}
          >
            {firmwareBlocked ? "Open Firmware Recovery" : "Open Firmware Updates"}
          </Button>
        )}

        <Button
          variant="ghost"
          size="sm"
          className="h-7 gap-1.5 px-2 text-xs text-warning hover:bg-warning/15 dark:text-warning"
          disabled={refreshBusy}
          onClick={() => {
            void appUpdateQ.refetch();
            if (firmwareVersion || updaterProtocol != null) {
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
