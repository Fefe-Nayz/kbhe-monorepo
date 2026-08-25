import { useState, useCallback, useEffect, useRef } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useNavigate } from "react-router-dom";
import { isTauri, invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { open as tauriOpen } from "@tauri-apps/plugin-dialog";
import { readFile } from "@tauri-apps/plugin-fs";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { kbheFirmware, resolveFirmwareVersion, type FirmwareResolveResult } from "@/lib/kbhe/firmware";
import {
  checkFirmwareUpdate,
  downloadFirmwareRelease,
} from "@/lib/kbhe/releases";
import { kbheTransport, selectKbheRecoveryTarget } from "@/lib/kbhe/transport";
import { formatFirmwareVersion, type FirmwareVersion } from "@/lib/kbhe/protocol";
import {
  UPDATER_MIGRATION_QUERY_KEY,
  captureUpdaterMigrationBackup,
  getUpdaterMigrationBackup,
  hasCompleteProfileRecovery,
  restoreUpdaterMigrationBackup,
  type UpdaterMigrationBackup,
} from "@/lib/kbhe/calibration-migration";
import { Button } from "@/components/ui/button";
import { Progress } from "@/components/ui/progress";
import { Badge } from "@/components/ui/badge";
import { ScrollArea } from "@/components/ui/scroll-area";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import {
  IconUpload,
  IconAlertTriangle,
  IconFileUpload,
  IconCheck,
  IconDownload,
  IconRefresh,
  IconX,
  IconPlugConnected,
  IconPlugConnectedX,
} from "@tabler/icons-react";
import { cn } from "@/lib/utils";
import { PageContent } from "@/components/shared/PageLayout";
import { SectionCard } from "@/components/shared/SectionCard";
import { SliderField } from "@/components/shared/SliderField";
import { IconAdjustments, IconCloudDownload } from "@tabler/icons-react";

type FlashState = "idle" | "flashing" | "success" | "error";
type FlashResult = { ok: true } | { ok: false; error: string };

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

export default function Firmware() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const { status, deviceInfo, firmwareVersion, compatibility, developerMode, ramOnlyMode } = useDeviceSession();
  const sessionDetected = status === "connected"
    || status === "updater"
    || status === "recovery-only";

  const recoveryDevicesQ = useQuery({
    queryKey: ["firmware", "recoveryDevices", deviceInfo?.serialNumber ?? null],
    queryFn: () => kbheTransport.listDevices(),
    enabled: isTauri(),
    refetchInterval: 2000,
    staleTime: 1000,
  });
  const sessionSerialNumber = deviceInfo?.serialNumber?.trim() || null;
  const recoveryTarget = selectKbheRecoveryTarget(
    recoveryDevicesQ.data ?? [],
    sessionSerialNumber,
  );
  const recoverySerialNumber = sessionSerialNumber
    || (recoveryTarget.state === "ready" ? recoveryTarget.device.serialNumber?.trim() : null)
    || null;
  const connected = Boolean(recoverySerialNumber);

  const bootloaderPresenceQ = useQuery({
    queryKey: ["firmware", "bootloaderPresence"],
    queryFn: () => kbheTransport.detectBootloaderPresence(),
    enabled: isTauri() && !sessionDetected,
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const firmwareUpdateQ = useQuery({
    queryKey: ["release", "firmware", firmwareVersion, compatibility?.updaterProtocol ?? null],
    queryFn: () => checkFirmwareUpdate(firmwareVersion, compatibility?.updaterProtocol),
    enabled: isTauri(),
    refetchInterval: 30 * 60 * 1000,
    staleTime: 10 * 60 * 1000,
  });

  const bootloaderDetected = !sessionDetected && Boolean(bootloaderPresenceQ.data);
  const connectedForStatus = sessionDetected || connected || bootloaderDetected;
  const updateModeDetected = status === "updater"
    || (status === "recovery-only" && deviceInfo?.kind === "updater")
    || bootloaderDetected;

  const calibrationRecoveryQ = useQuery({
    queryKey: [...UPDATER_MIGRATION_QUERY_KEY, recoverySerialNumber],
    queryFn: () => getUpdaterMigrationBackup(recoverySerialNumber!),
    enabled: isTauri() && Boolean(recoverySerialNumber),
    staleTime: 1_000,
  });

  const [firmwareBytes, setFirmwareBytes] = useState<Uint8Array | null>(null);
  const [firmwareSignature, setFirmwareSignature] = useState<Uint8Array | null>(null);
  const [fileName, setFileName] = useState<string | null>(null);
  const [filePath, setFilePath] = useState<string | null>(null);
  const [signaturePath, setSignaturePath] = useState<string | null>(null);
  const [fileVersion, setFileVersion] = useState<FirmwareResolveResult | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [flashState, setFlashState] = useState<FlashState>("idle");
  const [flashProgress, setFlashProgress] = useState(0);
  const [flashLog, setFlashLog] = useState<string[]>([]);
  const [firmwareUpdateError, setFirmwareUpdateError] = useState<string | null>(null);
  const [firmwareUpdateState, setFirmwareUpdateState] = useState<"idle" | "downloading" | "flashing" | "error">("idle");
  const [confirmOpen, setConfirmOpen] = useState(false);
  const [timeoutSec, setTimeoutSec] = useState(5);
  const [retries, setRetries] = useState(5);
  const [isDragOver, setIsDragOver] = useState(false);
  const dragDepthRef = useRef(0);
  const dropZoneRef = useRef<HTMLElement | null>(null);
  const logEndRef = useRef<HTMLSpanElement | null>(null);
  const flashInFlightRef = useRef(false);

  const appendLog = useCallback((msg: string) => {
    setFlashLog((prev) => [...prev, `[${new Date().toLocaleTimeString()}] ${msg}`]);
  }, []);

  const processSelectedFirmware = useCallback((
    bytes: Uint8Array,
    signature: Uint8Array | null,
    name: string,
    path?: string | null,
    detachedSignaturePath?: string | null,
  ) => {
    setFileError(null);
    setFileVersion(null);
    setFlashState("idle");
    setFlashLog([]);
    setFlashProgress(0);

    setFirmwareBytes(bytes);
    setFirmwareSignature(signature);
    setFileName(name);
    setFilePath(path ?? null);
    setSignaturePath(detachedSignaturePath ?? null);

    try {
      const versionInfo = resolveFirmwareVersion(bytes);
      setFileVersion(versionInfo);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
    }
    if (!signature || signature.length !== 64) {
      setFileError("A matching 64-byte Ed25519 .sig file is required before flashing.");
    }
  }, []);

  const hasBinExtension = useCallback((value: string): boolean => {
    return value.toLowerCase().endsWith(".bin");
  }, []);

  const parseDroppedPathText = useCallback((raw: string): string | null => {
    const firstLine = raw
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line.length > 0 && !line.startsWith("#"));

    if (!firstLine) {
      return null;
    }

    const normalizedLine = firstLine.replace(/^"+|"+$/g, "");

    if (normalizedLine.startsWith("file://")) {
      try {
        const parsed = new URL(normalizedLine);
        let path = decodeURIComponent(parsed.pathname);

        if (parsed.host) {
          const normalized = path.replace(/\//g, "\\");
          return `\\\\${parsed.host}${normalized}`;
        }

        if (/^\/[A-Za-z]:/.test(path)) {
          path = path.slice(1);
        }

        return path;
      } catch {
        return null;
      }
    }

    return normalizedLine;
  }, []);

  const readDetachedSignature = useCallback(async (
    firmwarePath: string,
    explicitSignaturePath?: string,
  ): Promise<{ bytes: Uint8Array; path: string } | null> => {
    const siblingPath = `${firmwarePath}.sig`;
    if (isTauri() && (!explicitSignaturePath || explicitSignaturePath === siblingPath)) {
      try {
        const bytes = new Uint8Array(await invoke<number[]>("kbhe_read_firmware_signature", {
          firmwarePath,
        }));
        if (bytes.length === 64) {
          return { bytes, path: siblingPath };
        }
      } catch {
        // Fall back to the plugin scope. Dialog/drop selections are scoped by Tauri.
      }
    }

    const candidates = explicitSignaturePath
      ? [explicitSignaturePath]
      : [siblingPath];
    for (const candidate of candidates) {
      try {
        const bytes = new Uint8Array(await readFile(candidate));
        if (bytes.length === 64) {
          return { bytes, path: candidate };
        }
      } catch {
        // Try the next deterministic sibling candidate.
      }
    }
    return null;
  }, []);

  const extractDroppedPath = useCallback((event: React.DragEvent<HTMLDivElement>): string | null => {
    const uriList = event.dataTransfer.getData("text/uri-list");
    const parsedUriList = parseDroppedPathText(uriList);
    if (parsedUriList) {
      return parsedUriList;
    }

    const textPlain = event.dataTransfer.getData("text/plain");
    return parseDroppedPathText(textPlain);
  }, [parseDroppedPathText]);

  const processDroppedPath = useCallback(async (rawPath: string) => {
    const droppedPath = parseDroppedPathText(rawPath) ?? rawPath;
    const name = droppedPath.split(/[\\/]/).pop() ?? droppedPath;

    if (!hasBinExtension(name)) {
      setFileError("Please drop a .bin firmware file.");
      return;
    }

    try {
      const bytes = await readFile(droppedPath);
      const signature = await readDetachedSignature(droppedPath);
      processSelectedFirmware(
        new Uint8Array(bytes),
        signature?.bytes ?? null,
        name,
        droppedPath,
        signature?.path,
      );
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
    }
  }, [hasBinExtension, parseDroppedPathText, processSelectedFirmware, readDetachedSignature]);

  const processDroppedPaths = useCallback(async (paths: string[]) => {
    if (paths.length === 0) {
      setFileError("No readable file found in drop data.");
      return;
    }

    const binPath = paths.find((path) => {
      const normalized = parseDroppedPathText(path) ?? path;
      const name = normalized.split(/[\\/]/).pop() ?? normalized;
      return hasBinExtension(name);
    });

    await processDroppedPath(binPath ?? paths[0]);
  }, [hasBinExtension, parseDroppedPathText, processDroppedPath]);

  const isPointInDropZone = useCallback((x: number, y: number): boolean => {
    const zone = dropZoneRef.current;
    if (!zone) {
      return false;
    }

    const rect = zone.getBoundingClientRect();
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
  }, []);

  const isTauriPointInDropZone = useCallback((x: number, y: number): boolean => {
    if (isPointInDropZone(x, y)) {
      return true;
    }

    const dpr = window.devicePixelRatio || 1;
    return isPointInDropZone(x / dpr, y / dpr);
  }, [isPointInDropZone]);

  const handleFilePick = useCallback(async () => {
    try {
      const selected = await tauriOpen({
        multiple: true,
        filters: [{ name: "Signed firmware", extensions: ["bin", "sig"] }],
      });
      if (!selected) return;

      const paths = typeof selected === "string" ? [selected] : selected;
      const filePath = paths.find((candidate) => candidate.toLowerCase().endsWith(".bin"));
      if (!filePath) {
        setFileError("Select a .bin firmware file and its matching .bin.sig file.");
        return;
      }
      const selectedSignaturePath = paths.find(
        (candidate) => candidate === `${filePath}.sig`,
      );

      const bytes = await readFile(filePath);
      const signature = await readDetachedSignature(filePath, selectedSignaturePath);
      const name = filePath.split(/[\\/]/).pop() ?? filePath;
      processSelectedFirmware(
        new Uint8Array(bytes),
        signature?.bytes ?? null,
        name,
        filePath,
        signature?.path,
      );
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
    }
  }, [processSelectedFirmware, readDetachedSignature]);

  const isFileDrag = useCallback((event: React.DragEvent<HTMLDivElement>): boolean => {
    const types = Array.from(event.dataTransfer?.types ?? []);
    return types.includes("Files") || types.includes("text/uri-list") || types.includes("text/plain");
  }, []);

  useEffect(() => {
    if (!isTauri()) {
      return;
    }

    let unlisten: (() => void) | undefined;

    const setup = async () => {
      try {
        unlisten = await getCurrentWindow().onDragDropEvent((event) => {
          const payload = event.payload;

          if (payload.type === "enter" || payload.type === "over") {
            setIsDragOver(isTauriPointInDropZone(payload.position.x, payload.position.y));
            return;
          }

          if (payload.type === "leave") {
            setIsDragOver(false);
            return;
          }

          setIsDragOver(false);
          if (payload.type === "drop") {
            if (!isTauriPointInDropZone(payload.position.x, payload.position.y)) {
              return;
            }
            void processDroppedPaths(payload.paths);
          }
        });
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setFileError(`Drag-and-drop initialization failed: ${msg}`);
      }
    };

    void setup();

    return () => {
      unlisten?.();
    };
  }, [isTauriPointInDropZone, processDroppedPaths]);

  const handleDragEnter = useCallback((event: React.DragEvent<HTMLDivElement>) => {
    if (!isFileDrag(event)) return;
    if (!isPointInDropZone(event.clientX, event.clientY)) return;
    event.preventDefault();
    event.stopPropagation();
    dragDepthRef.current += 1;
    setIsDragOver(true);
  }, [isFileDrag, isPointInDropZone]);

  const handleDragOver = useCallback((event: React.DragEvent<HTMLDivElement>) => {
    if (!isFileDrag(event)) return;

    const inDropZone = isPointInDropZone(event.clientX, event.clientY);
    if (!inDropZone) {
      if (isDragOver) {
        setIsDragOver(false);
      }
      return;
    }

    event.preventDefault();
    event.stopPropagation();
    event.dataTransfer.dropEffect = "copy";
    if (!isDragOver) {
      setIsDragOver(true);
    }
  }, [isDragOver, isFileDrag, isPointInDropZone]);

  const handleDragLeave = useCallback((event: React.DragEvent<HTMLDivElement>) => {
    if (!isFileDrag(event)) return;
    event.preventDefault();
    event.stopPropagation();
    dragDepthRef.current = Math.max(0, dragDepthRef.current - 1);
    if (dragDepthRef.current === 0) {
      setIsDragOver(false);
    }
  }, [isFileDrag]);

  const handleDrop = useCallback(async (event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    event.stopPropagation();
    dragDepthRef.current = 0;
    setIsDragOver(false);

    if (!isFileDrag(event)) {
      return;
    }

    if (!isPointInDropZone(event.clientX, event.clientY)) {
      return;
    }

    const files = Array.from(event.dataTransfer.files ?? []);

    if (files.length > 0) {
      const dropped = files.find((file) => hasBinExtension(file.name)) ?? files[0];
      const droppedPath = (dropped as File & { path?: string }).path;
      const droppedName = dropped.name || (droppedPath?.split(/[\\/]/).pop() ?? "");
      const detached = files.find((file) => file.name === `${droppedName}.sig`);
      const detachedPath = (detached as (File & { path?: string }) | undefined)?.path;

      if (!hasBinExtension(droppedName)) {
        setFileError("Please drop a .bin firmware file.");
        return;
      }

      try {
        if (droppedPath) {
          const bytes = await readFile(droppedPath);
          const signature = await readDetachedSignature(droppedPath, detachedPath);
          processSelectedFirmware(
            new Uint8Array(bytes),
            signature?.bytes ?? null,
            droppedName,
            droppedPath,
            signature?.path,
          );
          return;
        }

        const bytes = new Uint8Array(await dropped.arrayBuffer());
        const signature = detached ? new Uint8Array(await detached.arrayBuffer()) : null;
        processSelectedFirmware(bytes, signature, droppedName, null, null);
        return;
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setFileError(msg);
        return;
      }
    }

    const droppedPath = extractDroppedPath(event);
    if (!droppedPath) {
      setFileError("No readable file found in drop data.");
      return;
    }

    await processDroppedPath(droppedPath);
  }, [extractDroppedPath, hasBinExtension, isFileDrag, isPointInDropZone, processDroppedPath, processSelectedFirmware, readDetachedSignature]);

  useEffect(() => {
    logEndRef.current?.scrollIntoView({ block: "end" });
  }, [flashLog]);

  const runFlash = useCallback(async (
    bytes: Uint8Array,
    path: string | null,
    versionInfo: FirmwareResolveResult | null,
    signature: Uint8Array | null,
    detachedSignaturePath: string | null,
    resetLog = true,
    migrationPath: string | null = null,
    migrationSignaturePath: string | null = null,
    bootloaderRefreshPath: string | null = null,
    bootloaderRefreshSignaturePath: string | null = null,
  ): Promise<FlashResult> => {
    if (flashInFlightRef.current) {
      return { ok: false, error: "A firmware update is already in progress" };
    }
    flashInFlightRef.current = true;
    setConfirmOpen(false);
    setFlashState("flashing");
    setFlashProgress(0);
    if (resetLog) {
      setFlashLog([]);
    }

    let flashTargetSerialNumber: string | undefined;
    let migrationBackup: UpdaterMigrationBackup | null = null;
    let sessionReconnected = false;
    const reconnectSession = async (requireRuntime = false): Promise<boolean> => {
      if (!flashTargetSerialNumber) return false;
      appendLog("Reconnecting device session...");
      await delay(500);
      let lastError = "The keyboard has not reappeared yet";
      for (let attempt = 0; attempt < 6; attempt += 1) {
        try {
          await DeviceSessionManager.connect(flashTargetSerialNumber);
          const session = useDeviceSession.getState();
          const serialMatches = session.deviceInfo?.serialNumber?.trim() === flashTargetSerialNumber;
          const recognized = session.status === "connected"
            || session.status === "updater"
            || session.status === "recovery-only";
          const runtimeReady = session.deviceInfo?.kind === "runtime";
          if (serialMatches && recognized && (!requireRuntime || runtimeReady)) {
            appendLog("Reconnected successfully.");
            sessionReconnected = true;
            return true;
          }
          lastError = requireRuntime
            ? "The keyboard runtime has not reappeared yet"
            : "The keyboard has not reappeared yet";
        } catch (reconnectError) {
          lastError = reconnectError instanceof Error ? reconnectError.message : String(reconnectError);
        }
        await delay(500);
      }
      appendLog(`Reconnect failed: ${lastError}`);
      if (requireRuntime) {
        throw new Error(
          `SETTINGS_RESTORE_REQUIRED: ${lastError}. The recovery backup was kept; reconnect this keyboard and use the recovery banner to restore it.`,
        );
      }
      return false;
    };

    try {
      await delay(0);
      const expectedSerialNumber = useDeviceSession.getState().deviceInfo?.serialNumber?.trim()
        || recoverySerialNumber
        || undefined;
      if (!expectedSerialNumber) {
        throw new Error("The connected keyboard does not expose a stable USB serial number; refusing an ambiguous flash target.");
      }
      flashTargetSerialNumber = expectedSerialNumber;

      const nativeFlash = Boolean(path && detachedSignaturePath && isTauri());
      if (nativeFlash) {
        const session = useDeviceSession.getState();
        const confirmedUpdaterV3 = session.deviceInfo?.kind === "updater"
          && session.compatibility?.updaterProtocol === 0x0003;
        try {
          migrationBackup = await getUpdaterMigrationBackup(expectedSerialNumber);
        } catch (error) {
          if (!confirmedUpdaterV3) throw error;
          appendLog("Settings recovery storage is unavailable, but updater v3 is confirmed; no storage-erasing migration is needed.");
        }
        const runtimeConnected = session.deviceInfo?.kind === "runtime"
          && session.deviceInfo.serialNumber?.trim() === expectedSerialNumber
          && (session.status === "connected" || session.status === "recovery-only");

        if (runtimeConnected && await kbheDevice.getRamOnlyMode() !== false) {
          throw new Error(
            "SETTINGS_BACKUP_REQUIRED: an app profile is active in RAM-only mode (or its state could not be verified). Return to an on-device profile before updater migration.",
          );
        }
        if (runtimeConnected && migrationBackup && !hasCompleteProfileRecovery(migrationBackup)) {
          appendLog("Restoring the legacy calibration-only backup before creating complete recovery data...");
          await restoreUpdaterMigrationBackup(expectedSerialNumber);
          migrationBackup = null;
        }
        if (!migrationBackup && runtimeConnected) {
          appendLog("Saving calibration and all four on-device profile slots before updater negotiation...");
          migrationBackup = await captureUpdaterMigrationBackup(expectedSerialNumber);
          appendLog("Complete calibration/profile backup persisted and verified.");
          void queryClient.invalidateQueries({ queryKey: UPDATER_MIGRATION_QUERY_KEY });
        } else if ((!migrationBackup || !hasCompleteProfileRecovery(migrationBackup)) && !confirmedUpdaterV3) {
          throw new Error(
            "SETTINGS_BACKUP_REQUIRED: updater v2 migration cannot start without complete calibration and four-slot profile recovery data. Return the keyboard to runtime mode, reconnect, and retry so every setting can be captured first.",
          );
        } else if (migrationBackup) {
          appendLog(hasCompleteProfileRecovery(migrationBackup)
            ? "Using the verified complete settings recovery backup already saved for this keyboard."
            : "Using a legacy calibration-only backup on confirmed updater v3; no storage-erasing migration is needed.");
        }
      }

      appendLog("Preparing flash session...");
      await DeviceSessionManager.disconnect();
      await delay(250);

      let finalVersion: FirmwareVersion;

      if (nativeFlash && path && detachedSignaturePath) {
        // Native fast path: entire flash loop runs in Rust (no IPC per packet)
        const { version: resolvedVersion } = resolveFirmwareVersion(bytes, versionInfo?.version);

        const phaseLabels: Record<string, string> = {
          connecting: "Entering bootloader mode…",
          hello: "Handshaking with bootloader…",
          compatibility: "Checking updater and artifact compatibility…",
          begin: "Starting firmware transfer…",
          authenticating: "Authenticating firmware signature…",
          legacyMigration: "Installing the signed updater v2-to-v3 migrator…",
          migration: "Migrator is installing and verifying updater v3…",
          migrationReady: "Updater v3 is ready for the final signed firmware…",
          bootloaderCheck: "Checking the resident updater version…",
          bootloaderRefresh: "Installing the signed updater v3 refresh…",
          bootloaderCurrent: "Resident updater is already current; skipping refresh…",
          finish: "Verifying firmware…",
          boot: "Rebooting device…",
        };

        let lastPhase = "";
        let lastPercent = -1;

        const unlisten = await listen<{
          phase: string;
          bytesDone: number;
          totalBytes: number;
          percent: number;
        }>("kbhe_flash_progress", ({ payload }) => {
          if (payload.phase !== "flashing" && payload.phase !== lastPhase) {
            const label = phaseLabels[payload.phase];
            if (label) appendLog(label);
            lastPhase = payload.phase;
          }
          if (payload.phase === "flashing" && payload.percent !== lastPercent) {
            if (payload.percent % 5 === 0 || payload.percent === 100) {
              appendLog(`Flashing: ${payload.bytesDone}/${payload.totalBytes} bytes (${payload.percent}%)`);
            }
            setFlashProgress(payload.percent);
            lastPercent = payload.percent;
          }
        });

        try {
          await invoke("kbhe_flash_firmware", {
            firmwarePath: path,
            firmwareSignaturePath: detachedSignaturePath,
            migrationFirmwarePath: migrationPath,
            migrationFirmwareSignaturePath: migrationSignaturePath,
            bootloaderRefreshPath,
            bootloaderRefreshSignaturePath,
            firmwareVersion: resolvedVersion,
            expectedSerialNumber,
            timeoutMs: timeoutSec * 1000,
            retries,
          });
        } finally {
          unlisten();
        }

        finalVersion = resolvedVersion;
      } else {
        // Fallback: TypeScript-based flash (browser or no file path)
        finalVersion = await kbheFirmware.flashFirmware(bytes, {
          signature: signature ?? undefined,
          expectedSerialNumber,
          timeoutMs: timeoutSec * 1000,
          retries,
          onLog: appendLog,
          onProgress: ({ percent }) => setFlashProgress(percent),
        });
      }

      if (migrationBackup) {
        await reconnectSession(true);
        appendLog(hasCompleteProfileRecovery(migrationBackup)
          ? "Restoring calibration and all on-device profiles, then verifying them semantically..."
          : "Restoring the legacy per-key calibration backup...");
        const restored = await restoreUpdaterMigrationBackup(expectedSerialNumber);
        if (!restored) {
          throw new Error("CALIBRATION_RESTORE_REQUIRED: the saved calibration disappeared before restoration");
        }
        appendLog(hasCompleteProfileRecovery(migrationBackup)
          ? "Calibration and all profile settings restored, persisted, and verified."
          : "Legacy calibration restored and verified on all 82 keys.");
        await queryClient.invalidateQueries({ queryKey: UPDATER_MIGRATION_QUERY_KEY });
      }

      appendLog(`Flash complete! Version: ${formatFirmwareVersion(finalVersion)}`);
      setFlashState("success");
      return { ok: true };
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      appendLog(`Error: ${msg}`);
      setFlashState("error");
      return { ok: false, error: msg };
    } finally {
      if (!sessionReconnected) {
        await reconnectSession();
      }
      void queryClient.invalidateQueries({ queryKey: UPDATER_MIGRATION_QUERY_KEY });
      flashInFlightRef.current = false;
    }
  }, [timeoutSec, retries, appendLog, queryClient, recoverySerialNumber]);

  const handleFlash = useCallback(async () => {
    if (!firmwareBytes) return;
    await runFlash(
      firmwareBytes,
      filePath,
      fileVersion,
      firmwareSignature,
      signaturePath,
    );
  }, [firmwareBytes, filePath, fileVersion, firmwareSignature, signaturePath, runFlash]);

  const handleFlashLatestFirmware = useCallback(async () => {
    const tag = firmwareUpdateQ.data?.tag;
    if (!tag) return;

    setFirmwareUpdateState("downloading");
    setFirmwareUpdateError(null);
    setFlashLog([]);
    setFlashProgress(0);

    try {
      appendLog(`Downloading firmware release ${tag}...`);
      const downloaded = await downloadFirmwareRelease(tag, compatibility?.updaterProtocol);
      appendLog(`Downloaded ${downloaded.fileName}.`);
      setFirmwareUpdateState("flashing");
      const flashResult = await runFlash(
        new Uint8Array(),
        downloaded.path,
        { version: downloaded.firmwareVersion, source: "authenticated release tag" },
        null,
        downloaded.signaturePath,
        false,
        downloaded.migrationPath,
        downloaded.migrationSignaturePath,
        downloaded.bootloaderRefreshPath,
        downloaded.bootloaderRefreshSignaturePath,
      );
      if (!flashResult.ok) {
        throw new Error(flashResult.error);
      }
      setFirmwareUpdateState("idle");
      await firmwareUpdateQ.refetch();
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFirmwareUpdateError(msg);
      setFirmwareUpdateState("error");
      setFlashState("error");
      appendLog(`Error: ${msg}`);
    }
  }, [appendLog, compatibility?.updaterProtocol, firmwareUpdateQ, runFlash]);

  const fileSizeKb = firmwareBytes ? (firmwareBytes.length / 1024).toFixed(1) : null;
  const compatibilityBlocksFlash = compatibility?.status === "app-too-old";
  const updaterV2NeedsCalibrationBackup = updateModeDetected
    && compatibility?.updaterProtocol === 0x0002;
  const ramOnlyProfileBlocksFlash = status === "connected" && ramOnlyMode === true;
  const calibrationMigrationBlocked = updaterV2NeedsCalibrationBackup
    && !hasCompleteProfileRecovery(calibrationRecoveryQ.data ?? null);
  const canFlash = connected
    && !compatibilityBlocksFlash
    && !calibrationMigrationBlocked
    && !ramOnlyProfileBlocksFlash
    && !!firmwareBytes
    && fileVersion !== null
    && fileError === null
    && firmwareSignature?.length === 64
    && flashState !== "flashing"
    && firmwareUpdateState !== "flashing";
  const latestFirmware = firmwareUpdateQ.data;
  const firmwareReleaseBlocked = latestFirmware?.blockedReason ?? null;
  const firmwareReleaseBusy = firmwareUpdateState === "downloading" || firmwareUpdateState === "flashing";

  return (
    <>
      <PageContent containerClassName="max-w-3xl">

        {/* ── Device status strip ─────────────────────────────── */}
        <div className="flex items-center gap-3.5 rounded-xl border bg-card p-4">
          <div className={cn(
            "flex size-11 shrink-0 items-center justify-center rounded-xl",
            updateModeDetected
              ? "bg-warning/12 text-warning"
              : connectedForStatus
                ? "bg-success/12 text-success"
                : "bg-muted text-muted-foreground",
          )}>
            {connectedForStatus
              ? <IconPlugConnected className="size-5" />
              : <IconPlugConnectedX className="size-5" />}
          </div>
          <div className="flex min-w-0 flex-1 flex-col gap-0.5">
            <span className="text-sm font-semibold">
              {updateModeDetected
                ? "Connected in update mode"
                : connectedForStatus
                  ? "Keyboard connected"
                  : "No keyboard connected"}
            </span>
            <span className="truncate text-xs text-muted-foreground">
              {updateModeDetected
                ? "The keyboard is in bootloader mode and ready to receive firmware."
                : firmwareVersion
                ? `Running firmware ${firmwareVersion}.`
                : connectedForStatus
                  ? "Firmware version could not be read."
                  : "Plug the keyboard in over USB to flash firmware."}
            </span>
          </div>
          {firmwareVersion && (
            <Badge variant="secondary" className="shrink-0 font-mono">{firmwareVersion}</Badge>
          )}
        </div>

        {ramOnlyProfileBlocksFlash && (
          <div className="flex items-start gap-2 rounded-lg border border-destructive/30 bg-destructive/10 px-3 py-2 text-xs leading-relaxed text-destructive" role="alert">
            <IconAlertTriangle className="mt-0.5 size-4 shrink-0" />
            <span>
              Firmware migration is blocked while an app profile is active in RAM-only mode.
              Return to an on-device profile first so the four durable slots can be captured exactly.
            </span>
          </div>
        )}

        {updaterV2NeedsCalibrationBackup && (
          <div
            className={cn(
              "flex items-start gap-2 rounded-lg border px-3 py-2 text-xs leading-relaxed",
              calibrationMigrationBlocked
                ? "border-destructive/30 bg-destructive/10 text-destructive"
                : "border-warning/30 bg-warning/10 text-warning",
            )}
            role={calibrationMigrationBlocked ? "alert" : "status"}
          >
            <IconAlertTriangle className="mt-0.5 size-4 shrink-0" />
            <span>
              {calibrationRecoveryQ.isLoading
                ? "Checking for the required complete pre-migration settings backup…"
                : calibrationRecoveryQ.error
                  ? `Calibration recovery storage could not be read: ${calibrationRecoveryQ.error instanceof Error ? calibrationRecoveryQ.error.message : String(calibrationRecoveryQ.error)}. Migration is blocked.`
                  : calibrationMigrationBlocked
                    ? "Updater v2 migration is blocked because no complete calibration plus four-profile backup exists. Return the keyboard to runtime mode, reconnect it, then start the update here so every setting can be saved first. A legacy calibration-only backup is preserved but is not sufficient to migrate."
                    : "The complete calibration and four-profile backup is safe. Migration can continue and the app will restore, persist, and verify every captured setting after runtime reconnects."}
            </span>
          </div>
        )}

        {/* -- Online firmware update -------------------------------- */}
        {isTauri() && (
          <SectionCard
            tone={firmwareReleaseBlocked ? "danger" : latestFirmware?.updateAvailable ? "accent" : "default"}
            title="Online firmware update"
            icon={<IconCloudDownload />}
            description={
              firmwareReleaseBlocked
                ? `Release ${latestFirmware?.tag ?? latestFirmware?.version ?? "latest"} is incomplete for this keyboard.`
                : latestFirmware?.updateAvailable
                ? `Release ${latestFirmware.tag ?? latestFirmware.version} is available.`
                : firmwareUpdateQ.isLoading
                  ? "Checking GitHub releases…"
                  : "The installed firmware is up to date."
            }
            headerRight={
              <Button
                variant="ghost"
                size="icon-sm"
                disabled={firmwareUpdateQ.isFetching || firmwareReleaseBusy}
                onClick={() => {
                  void firmwareUpdateQ.refetch();
                }}
                title="Check again"
              >
                <IconRefresh className="size-4" />
              </Button>
            }
          >
            {firmwareReleaseBlocked ? (
              <div className="flex flex-col gap-3" role="alert">
                <div className="flex items-start gap-2 rounded-md bg-destructive/10 px-3 py-2 text-xs leading-relaxed text-destructive">
                  <IconAlertTriangle className="mt-0.5 size-4 shrink-0" />
                  <span>{firmwareReleaseBlocked}</span>
                </div>
                <div className="flex flex-wrap items-center gap-3">
                  {compatibility?.updaterProtocol == null && status === "connected" && (
                    <Button
                      variant="outline"
                      size="sm"
                      onClick={() => navigate("/device")}
                    >
                      Verify updater protocol
                    </Button>
                  )}
                  <Button
                    variant="outline"
                    size="sm"
                    disabled={firmwareUpdateQ.isFetching}
                    onClick={() => void firmwareUpdateQ.refetch()}
                  >
                    <IconRefresh className="size-4" />
                    Check corrected release
                  </Button>
                  <span className="text-xs text-muted-foreground">
                    Do not select the normal application manually on updater v2.
                  </span>
                </div>
              </div>
            ) : latestFirmware?.updateAvailable ? (
              <div className="flex flex-col gap-3">
                <div className="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
                  {latestFirmware.version && (
                    <Badge variant="secondary" className="font-mono">{latestFirmware.version}</Badge>
                  )}
                  {latestFirmware.assetName && <span>{latestFirmware.assetName}</span>}
                </div>
                <div className="flex flex-wrap items-center gap-3">
                  <Button
                    disabled={!connected || compatibilityBlocksFlash || calibrationMigrationBlocked || ramOnlyProfileBlocksFlash || firmwareReleaseBusy || flashState === "flashing"}
                    onClick={() => void handleFlashLatestFirmware()}
                  >
                    <IconDownload className="size-4" />
                    {firmwareReleaseBusy ? "Updating…" : "Download and flash"}
                  </Button>
                  {(!connected || compatibilityBlocksFlash) && (
                    <span className="text-xs text-muted-foreground">
                      {compatibilityBlocksFlash
                        ? "Update the configurator before flashing this newer device."
                        : recoveryTarget.state === "unsafe"
                          ? recoveryTarget.reason
                          : "Connect a keyboard with a stable USB serial before flashing."}
                    </span>
                  )}
                </div>
                {firmwareUpdateError && (
                  <div className="flex items-center gap-2 rounded-md bg-destructive/10 px-3 py-2 text-xs text-destructive">
                    <IconAlertTriangle className="size-4 shrink-0" />
                    {firmwareUpdateError}
                  </div>
                )}
              </div>
            ) : (
              <p className="text-xs leading-relaxed text-muted-foreground">
                Updates are published as signed releases and verified before they are written
                to the keyboard. Nothing is flashed without your confirmation.
              </p>
            )}
          </SectionCard>
        )}

        {/* Without developer mode there is nothing else on this page — say so
            rather than leaving the rest of the screen blank. */}
        {!developerMode && !latestFirmware?.updateAvailable && (
          <SectionCard
            tone="muted"
            title="Manual flashing"
            description="Loading a firmware file by hand is a developer-mode feature."
            icon={<IconFileUpload />}
          >
            <p className="text-xs leading-relaxed text-muted-foreground">
              Turn on Developer mode in App Settings to pick a signed <code>.bin</code> and
              its <code>.bin.sig</code>, tune flash timeouts, and watch the flash log.
            </p>
          </SectionCard>
        )}

        {/* ── File picker area (developer mode) ───────────────── */}
        {developerMode && (
        <SectionCard
          title="Firmware file"
          description="Pick or drop a signed release pair to flash by hand."
          icon={<IconFileUpload />}
          contentClassName="px-5 py-4"
        >
          <div
            className="flex flex-col gap-4"
            onDragEnter={handleDragEnter}
            onDragOver={handleDragOver}
            onDragLeave={handleDragLeave}
            onDrop={(event) => {
              void handleDrop(event);
            }}
          >
            {!fileName ? (
              <>
                <button
                  ref={(node) => {
                    dropZoneRef.current = node;
                  }}
                  type="button"
                  onClick={() => void handleFilePick()}
                  className={cn(
                    "flex flex-col items-center justify-center gap-3 rounded-lg border-2 border-dashed border-muted-foreground/25 py-10 text-muted-foreground hover:border-primary/40 hover:text-foreground transition-colors cursor-pointer",
                    isDragOver && "border-primary bg-primary/5 text-foreground ring-2 ring-primary/30 ring-inset",
                  )}
                >
                  <IconFileUpload className="size-8" />
                  <div className="text-center">
                    <p className="text-sm font-medium">Select signed firmware</p>
                    <p className="text-xs mt-0.5">Choose the .bin and matching .bin.sig files</p>
                  </div>
                </button>
                <p className="text-center text-xs text-muted-foreground">
                  Or drag and drop both signed release files here
                </p>
              </>
            ) : (
              <div className="flex flex-col gap-3">
                <div className="flex items-start gap-3">
                  <div className="flex size-10 items-center justify-center rounded-lg bg-muted shrink-0">
                    <IconFileUpload className="size-5 text-muted-foreground" />
                  </div>
                  <div className="flex-1 min-w-0">
                    <p className="text-sm font-medium truncate">{fileName}</p>
                    <div className="flex items-center gap-3 mt-0.5 text-xs text-muted-foreground">
                      {fileSizeKb && <span>{fileSizeKb} KB</span>}
                      {fileVersion && (
                        <Badge variant="outline" className="font-mono text-[10px]">
                          {formatFirmwareVersion(fileVersion.version)}
                        </Badge>
                      )}
                      {fileVersion?.source && (
                        <span className="text-muted-foreground/60">{fileVersion.source}</span>
                      )}
                      {firmwareSignature?.length === 64 && (
                        <Badge variant="outline" className="border-success/40 text-[0.65rem] text-success">
                          Signed
                        </Badge>
                      )}
                    </div>
                  </div>
                  <div className="flex gap-1.5 shrink-0">
                    <Button
                      variant="outline"
                      size="sm"
                      className="h-8"
                      onClick={() => void handleFilePick()}
                    >
                      Change
                    </Button>
                    <Button
                      variant="ghost"
                      size="icon"
                      className="size-8"
                      onClick={() => {
                        setFirmwareBytes(null);
                        setFirmwareSignature(null);
                        setFileName(null);
                        setFilePath(null);
                        setSignaturePath(null);
                        setFileVersion(null);
                        setFileError(null);
                        setFlashState("idle");
                        setFlashLog([]);
                      }}
                    >
                      <IconX className="size-4" />
                    </Button>
                  </div>
                </div>

                {fileError && (
                  <div className="flex items-center gap-2 rounded-md bg-destructive/10 px-3 py-2 text-destructive text-xs">
                    <IconAlertTriangle className="size-4 shrink-0" />
                    {fileError}
                  </div>
                )}

                {fileVersion && firmwareVersion && (
                  <div className="flex items-center gap-2 rounded-md bg-muted px-3 py-2 text-xs">
                    <span className="text-muted-foreground">Upgrade path:</span>
                    <span className="font-mono">{firmwareVersion}</span>
                    <span className="text-muted-foreground">→</span>
                    <span className="font-mono font-medium">{formatFirmwareVersion(fileVersion.version)}</span>
                  </div>
                )}

                <div
                  ref={(node) => {
                    dropZoneRef.current = node;
                  }}
                  className={cn(
                    "rounded-md border border-dashed border-muted-foreground/25 px-3 py-2 text-xs text-muted-foreground transition-colors",
                    isDragOver && "border-primary bg-primary/5 text-foreground ring-2 ring-primary/30 ring-inset",
                  )}
                >
                  Drag and drop another .bin + .bin.sig pair here to replace it
                </div>
              </div>
            )}
          </div>
        </SectionCard>
        )}

        {/* ── Flash options (developer mode) ─────────────────── */}
        {developerMode && (
          <SectionCard
            title="Flash options"
            description="How patiently the flasher waits on each bootloader operation."
            icon={<IconAdjustments />}
          >
            <div className="flex flex-col gap-5">
              <SliderField
                label="Timeout per operation"
                description="Give up on a single bootloader exchange after this long."
                unit="s"
                min={1} max={30} step={1}
                value={timeoutSec}
                onCommit={setTimeoutSec}
                disabled={flashState === "flashing"}
              />
              <SliderField
                label="Retry attempts"
                description="How many times to retry a failed exchange before aborting."
                min={1} max={20} step={1}
                value={retries}
                onCommit={setRetries}
                disabled={flashState === "flashing"}
              />
            </div>
          </SectionCard>
        )}

        {/* ── Flash action + progress ─────────────────────────── */}
        {(developerMode || flashState !== "idle" || flashLog.length > 0) && (
        <SectionCard
          title="Flash"
          description="Write the selected firmware to the keyboard."
          icon={<IconUpload />}
        >
          <div className="flex flex-col gap-4">
            <div className="flex items-center gap-3">
              {developerMode && (
                <Button
                  disabled={!canFlash}
                  onClick={() => setConfirmOpen(true)}
                >
                  <IconUpload className="size-4" />
                  Flash firmware
                </Button>
              )}

              {flashState === "flashing" && (
                <span className="text-xs text-muted-foreground animate-pulse">Flashing… {flashProgress}%</span>
              )}
              {flashState === "success" && (
                <Badge className="gap-1 bg-success text-success-foreground">
                  <IconCheck className="size-3" />
                  Complete
                </Badge>
              )}
              {flashState === "error" && (
                <Badge variant="destructive" className="gap-1">
                  <IconX className="size-3" />
                  Failed
                </Badge>
              )}
            </div>

            {flashState !== "idle" && (
              <Progress value={flashProgress} className="h-2" />
            )}

            {flashLog.length > 0 && (
              <div>
                <p className="mb-1.5 text-[0.68rem] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
                  Flash log
                </p>
                <ScrollArea className="h-44 rounded-lg border bg-surface-sunken p-3">
                  <pre className="whitespace-pre-wrap font-mono text-xs text-muted-foreground">
                    {flashLog.join("\n")}
                    <span ref={logEndRef} />
                  </pre>
                </ScrollArea>
              </div>
            )}
          </div>
        </SectionCard>
        )}

      </PageContent>

      {/* ── Confirm dialog ──────────────────────────────────── */}
      <Dialog open={confirmOpen} onOpenChange={setConfirmOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <IconAlertTriangle className="size-5 text-destructive" />
              Flash this firmware?
            </DialogTitle>
            <DialogDescription>
              {fileVersion && (
                <span className="block mb-2">
                  Flashing version <strong>{formatFirmwareVersion(fileVersion.version)}</strong>
                  {firmwareVersion && <> (currently {firmwareVersion})</>}
                </span>
              )}
              This will reboot the keyboard into bootloader mode and flash new firmware.
              The keyboard will be temporarily unavailable. Do not unplug during the process.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" onClick={() => setConfirmOpen(false)}>Cancel</Button>
            <Button variant="destructive" onClick={() => void handleFlash()}>
              Flash now
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </>
  );
}
