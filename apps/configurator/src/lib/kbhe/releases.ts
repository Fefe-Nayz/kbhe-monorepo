import { getVersion } from "@tauri-apps/api/app";
import { invoke, isTauri } from "@tauri-apps/api/core";

export interface ReleaseUpdateInfo {
  updateAvailable: boolean;
  blockedReason: string | null;
  migrationRequired: boolean;
  migrationAvailable: boolean;
  bootloaderRefreshAvailable: boolean;
  version: string | null;
  tag: string | null;
  name: string | null;
  notes: string | null;
  publishedAt: string | null;
  htmlUrl: string | null;
  assetName: string | null;
  assetSize: number | null;
}

type UpdaterRecoveryAvailability = Pick<
  ReleaseUpdateInfo,
  "blockedReason" | "migrationAvailable" | "bootloaderRefreshAvailable"
>;

/**
 * Identifying an unknown resident updater is useful only when exactly one of
 * the two updater-specific recovery paths exists in the blocked release.
 */
export function canUpdaterIdentificationUnlockRelease(
  update: UpdaterRecoveryAvailability | null | undefined,
  updaterProtocol: number | null | undefined,
): boolean {
  return updaterProtocol == null
    && update?.blockedReason != null
    && update.migrationAvailable !== update.bootloaderRefreshAvailable;
}

export interface DownloadedFirmware {
  path: string;
  signaturePath: string;
  fileName: string;
  versionTag: string;
  firmwareVersion: {
    major: number;
    minor: number;
    patch: number;
  };
  migrationPath: string | null;
  migrationSignaturePath: string | null;
  bootloaderRefreshPath: string | null;
  bootloaderRefreshSignaturePath: string | null;
}

export async function checkAppUpdate(): Promise<ReleaseUpdateInfo> {
  const currentVersion = isTauri() ? await getVersion() : undefined;
  return invoke<ReleaseUpdateInfo>("kbhe_check_app_update", { currentVersion });
}

export async function checkFirmwareUpdate(
  currentVersion?: string | null,
  updaterProtocol?: number | null,
): Promise<ReleaseUpdateInfo> {
  return invoke<ReleaseUpdateInfo>("kbhe_check_firmware_update", {
    currentVersion: currentVersion ?? undefined,
    updaterProtocol: updaterProtocol ?? undefined,
  });
}

export async function downloadFirmwareRelease(
  tag: string,
  updaterProtocol?: number | null,
  appOnlyRecovery = false,
): Promise<DownloadedFirmware> {
  return invoke<DownloadedFirmware>("kbhe_download_firmware_release", {
    tag,
    updaterProtocol: updaterProtocol ?? undefined,
    appOnlyRecovery,
  });
}

export async function downloadAndRunAppInstaller(tag: string): Promise<string> {
  return invoke<string>("kbhe_download_and_run_app_installer", { tag });
}
