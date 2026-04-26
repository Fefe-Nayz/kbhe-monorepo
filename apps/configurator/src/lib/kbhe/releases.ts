import { getVersion } from "@tauri-apps/api/app";
import { invoke, isTauri } from "@tauri-apps/api/core";

export interface ReleaseUpdateInfo {
  updateAvailable: boolean;
  version: string | null;
  tag: string | null;
  name: string | null;
  notes: string | null;
  publishedAt: string | null;
  htmlUrl: string | null;
  assetName: string | null;
  assetSize: number | null;
}

export interface DownloadedFirmware {
  path: string;
  fileName: string;
  versionTag: string;
}

export async function checkAppUpdate(): Promise<ReleaseUpdateInfo> {
  const currentVersion = isTauri() ? await getVersion() : undefined;
  return invoke<ReleaseUpdateInfo>("kbhe_check_app_update", { currentVersion });
}

export async function checkFirmwareUpdate(currentVersion?: string | null): Promise<ReleaseUpdateInfo> {
  return invoke<ReleaseUpdateInfo>("kbhe_check_firmware_update", {
    currentVersion: currentVersion ?? undefined,
  });
}

export async function downloadFirmwareRelease(tag: string): Promise<DownloadedFirmware> {
  return invoke<DownloadedFirmware>("kbhe_download_firmware_release", { tag });
}

export async function downloadAndRunAppInstaller(tag: string): Promise<string> {
  return invoke<string>("kbhe_download_and_run_app_installer", { tag });
}
