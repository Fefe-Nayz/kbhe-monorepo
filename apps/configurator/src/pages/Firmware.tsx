import { useState, useCallback, useEffect, useRef } from "react";
import { useQuery } from "@tanstack/react-query";
import { isTauri, invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { open as tauriOpen } from "@tauri-apps/plugin-dialog";
import { readFile } from "@tauri-apps/plugin-fs";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheFirmware, resolveFirmwareVersion, type FirmwareResolveResult } from "@/lib/kbhe/firmware";
import {
  checkFirmwareUpdate,
  downloadFirmwareRelease,
} from "@/lib/kbhe/releases";
import { kbheTransport } from "@/lib/kbhe/transport";
import { formatFirmwareVersion, type FirmwareVersion } from "@/lib/kbhe/protocol";
import { Button } from "@/components/ui/button";
import { Label } from "@/components/ui/label";
import { Progress } from "@/components/ui/progress";
import { Badge } from "@/components/ui/badge";
import { Slider } from "@/components/ui/slider";
import { ScrollArea } from "@/components/ui/scroll-area";
import { sliderVal } from "@/lib/utils";
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

type FlashState = "idle" | "flashing" | "success" | "error";

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

export default function Firmware() {
  const { status, firmwareVersion, developerMode } = useDeviceSession();
  const connected = status === "connected" || status === "updater";

  const bootloaderPresenceQ = useQuery({
    queryKey: ["firmware", "bootloaderPresence"],
    queryFn: () => kbheTransport.detectBootloaderPresence(),
    enabled: isTauri() && !connected,
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const firmwareUpdateQ = useQuery({
    queryKey: ["release", "firmware", firmwareVersion],
    queryFn: () => checkFirmwareUpdate(firmwareVersion),
    enabled: isTauri(),
    refetchInterval: 30 * 60 * 1000,
    staleTime: 10 * 60 * 1000,
  });

  const bootloaderDetected = !connected && Boolean(bootloaderPresenceQ.data);
  const connectedForStatus = connected || bootloaderDetected;
  const updateModeDetected = status === "updater" || bootloaderDetected;

  const [firmwareBytes, setFirmwareBytes] = useState<Uint8Array | null>(null);
  const [fileName, setFileName] = useState<string | null>(null);
  const [filePath, setFilePath] = useState<string | null>(null);
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

  const appendLog = useCallback((msg: string) => {
    setFlashLog((prev) => [...prev, `[${new Date().toLocaleTimeString()}] ${msg}`]);
  }, []);

  const processSelectedFirmware = useCallback((bytes: Uint8Array, name: string, path?: string | null) => {
    setFileError(null);
    setFileVersion(null);
    setFlashState("idle");
    setFlashLog([]);
    setFlashProgress(0);

    setFirmwareBytes(bytes);
    setFileName(name);
    setFilePath(path ?? null);

    try {
      const versionInfo = resolveFirmwareVersion(bytes);
      setFileVersion(versionInfo);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
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
      processSelectedFirmware(new Uint8Array(bytes), name, droppedPath);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
    }
  }, [hasBinExtension, parseDroppedPathText, processSelectedFirmware]);

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
        multiple: false,
        filters: [{ name: "Firmware", extensions: ["bin"] }],
      });
      if (!selected) return;

      const filePath = typeof selected === "string" ? selected : selected;

      const bytes = await readFile(filePath);
      const name = filePath.split(/[\\/]/).pop() ?? filePath;
      processSelectedFirmware(new Uint8Array(bytes), name, filePath);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
    }
  }, [processSelectedFirmware]);

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

      if (!hasBinExtension(droppedName)) {
        setFileError("Please drop a .bin firmware file.");
        return;
      }

      try {
        if (droppedPath) {
          const bytes = await readFile(droppedPath);
          processSelectedFirmware(new Uint8Array(bytes), droppedName, droppedPath);
          return;
        }

        const bytes = new Uint8Array(await dropped.arrayBuffer());
        processSelectedFirmware(bytes, droppedName, null);
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
  }, [extractDroppedPath, hasBinExtension, isFileDrag, isPointInDropZone, processDroppedPath, processSelectedFirmware]);

  useEffect(() => {
    logEndRef.current?.scrollIntoView({ block: "end" });
  }, [flashLog]);

  const runFlash = useCallback(async (
    bytes: Uint8Array,
    path: string | null,
    versionInfo: FirmwareResolveResult | null,
    resetLog = true,
  ) => {
    setConfirmOpen(false);
    setFlashState("flashing");
    setFlashProgress(0);
    if (resetLog) {
      setFlashLog([]);
    }

    const reconnectSession = async () => {
      appendLog("Reconnecting device session...");
      await delay(1200);
      try {
        await DeviceSessionManager.connect();
        appendLog("Reconnected successfully.");
      } catch (reconnectError) {
        const reconnectMsg = reconnectError instanceof Error ? reconnectError.message : String(reconnectError);
        appendLog(`Reconnect failed: ${reconnectMsg}`);
      }
    };

    try {
      await delay(0);
      appendLog("Preparing flash session...");
      await DeviceSessionManager.disconnect();
      await delay(250);

      let finalVersion: FirmwareVersion;

      if (path && isTauri()) {
        // Native fast path: entire flash loop runs in Rust (no IPC per packet)
        const { version: resolvedVersion } = resolveFirmwareVersion(bytes, versionInfo?.version);

        const phaseLabels: Record<string, string> = {
          connecting: "Entering bootloader mode…",
          hello: "Handshaking with bootloader…",
          begin: "Starting firmware transfer…",
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
            firmwareVersion: resolvedVersion,
          });
        } finally {
          unlisten();
        }

        finalVersion = resolvedVersion;
      } else {
        // Fallback: TypeScript-based flash (browser or no file path)
        finalVersion = await kbheFirmware.flashFirmware(bytes, {
          timeoutMs: timeoutSec * 1000,
          retries,
          onLog: appendLog,
          onProgress: ({ percent }) => setFlashProgress(percent),
        });
      }

      appendLog(`Flash complete! Version: ${formatFirmwareVersion(finalVersion)}`);
      setFlashState("success");
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      appendLog(`Error: ${msg}`);
      setFlashState("error");
    } finally {
      void reconnectSession();
    }
  }, [timeoutSec, retries, appendLog]);

  const handleFlash = useCallback(async () => {
    if (!firmwareBytes) return;
    await runFlash(firmwareBytes, filePath, fileVersion);
  }, [firmwareBytes, filePath, fileVersion, runFlash]);

  const handleFlashLatestFirmware = useCallback(async () => {
    const tag = firmwareUpdateQ.data?.tag;
    if (!tag) return;

    setFirmwareUpdateState("downloading");
    setFirmwareUpdateError(null);
    setFlashLog([]);
    setFlashProgress(0);

    try {
      appendLog(`Downloading firmware release ${tag}...`);
      const downloaded = await downloadFirmwareRelease(tag);
      const bytes = new Uint8Array(await readFile(downloaded.path));
      const versionInfo = resolveFirmwareVersion(bytes);

      processSelectedFirmware(bytes, downloaded.fileName, downloaded.path);
      appendLog(`Downloaded ${downloaded.fileName}.`);
      setFirmwareUpdateState("flashing");
      await runFlash(bytes, downloaded.path, versionInfo, false);
      setFirmwareUpdateState("idle");
      await firmwareUpdateQ.refetch();
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFirmwareUpdateError(msg);
      setFirmwareUpdateState("error");
      setFlashState("error");
      appendLog(`Error: ${msg}`);
    }
  }, [appendLog, firmwareUpdateQ, processSelectedFirmware, runFlash]);

  const fileSizeKb = firmwareBytes ? (firmwareBytes.length / 1024).toFixed(1) : null;
  const canFlash = connected && !!firmwareBytes && flashState !== "flashing" && firmwareUpdateState !== "flashing";
  const latestFirmware = firmwareUpdateQ.data;
  const firmwareReleaseBusy = firmwareUpdateState === "downloading" || firmwareUpdateState === "flashing";

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent containerClassName="max-w-3xl">

        {/* ── Device status strip ─────────────────────────────── */}
        <div className="flex items-center gap-3 rounded-lg border bg-card p-4">
          <div className={cn(
            "flex size-10 items-center justify-center rounded-full",
            connectedForStatus ? "bg-green-500/10 text-green-600 dark:text-green-400" : "bg-muted text-muted-foreground",
          )}>
            {connectedForStatus
              ? <IconPlugConnected className="size-5" />
              : <IconPlugConnectedX className="size-5" />}
          </div>
          <div className="flex flex-col gap-0.5 flex-1 min-w-0">
            <span className="text-sm font-medium">
              {updateModeDetected ? "Device Connected (Update Mode)" : connectedForStatus ? "Device Connected" : "No Device"}
            </span>
            <span className="text-xs text-muted-foreground truncate">
              {updateModeDetected
                ? "Keyboard detected in bootloader mode. Ready for firmware update."
                : firmwareVersion
                ? `Installed firmware: ${firmwareVersion}`
                : connectedForStatus
                  ? "Firmware version unknown"
                  : "Connect your keyboard to flash firmware"}
            </span>
          </div>
          {firmwareVersion && (
            <Badge variant="secondary" className="font-mono shrink-0">{firmwareVersion}</Badge>
          )}
        </div>

        {/* -- Online firmware update -------------------------------- */}
        {isTauri() && (
          <div className="rounded-lg border bg-card">
            <div className="flex items-center justify-between gap-3 border-b px-4 py-3">
              <div>
                <h2 className="text-sm font-medium">Online Firmware Update</h2>
                <p className="mt-0.5 text-xs text-muted-foreground">
                  {latestFirmware?.updateAvailable
                    ? `Release ${latestFirmware.tag ?? latestFirmware.version} is available.`
                    : firmwareUpdateQ.isLoading
                      ? "Checking GitHub releases..."
                      : "Firmware is up to date."}
                </p>
              </div>
              <Button
                variant="ghost"
                size="icon"
                disabled={firmwareUpdateQ.isFetching || firmwareReleaseBusy}
                onClick={() => {
                  void firmwareUpdateQ.refetch();
                }}
                title="Check again"
              >
                <IconRefresh className="size-4" />
              </Button>
            </div>

            {latestFirmware?.updateAvailable && (
              <div className="flex flex-col gap-3 p-4">
                <div className="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
                  {latestFirmware.version && (
                    <Badge variant="secondary" className="font-mono">{latestFirmware.version}</Badge>
                  )}
                  {latestFirmware.assetName && <span>{latestFirmware.assetName}</span>}
                </div>
                <div className="flex flex-wrap items-center gap-3">
                  <Button
                    className="gap-2"
                    disabled={!connected || firmwareReleaseBusy || flashState === "flashing"}
                    onClick={() => void handleFlashLatestFirmware()}
                  >
                    <IconDownload className="size-4" />
                    {firmwareReleaseBusy ? "Updating..." : "Download and Flash"}
                  </Button>
                  {!connected && (
                    <span className="text-xs text-muted-foreground">Connect the keyboard before flashing.</span>
                  )}
                </div>
                {firmwareUpdateError && (
                  <div className="flex items-center gap-2 rounded-md bg-destructive/10 px-3 py-2 text-xs text-destructive">
                    <IconAlertTriangle className="size-4 shrink-0" />
                    {firmwareUpdateError}
                  </div>
                )}
              </div>
            )}
          </div>
        )}

        {/* ── File picker area (developer mode) ───────────────── */}
        {developerMode && (
        <div className="rounded-lg border bg-card">
          <div className="border-b px-4 py-3">
            <h2 className="text-sm font-medium">Firmware File</h2>
          </div>
          <div
            className="p-4 flex flex-col gap-4"
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
                    <p className="text-sm font-medium">Select firmware binary</p>
                    <p className="text-xs mt-0.5">Choose a .bin file to flash</p>
                  </div>
                </button>
                <p className="text-center text-xs text-muted-foreground">
                  Or drag and drop a .bin file here
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
                        setFileName(null);
                        setFilePath(null);
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
                  Drag and drop another .bin file here to replace it
                </div>
              </div>
            )}
          </div>
        </div>
        )}

        {/* ── Flash options (developer mode) ─────────────────── */}
        {developerMode && (
          <div className="rounded-lg border bg-card">
            <div className="border-b px-4 py-3">
              <h2 className="text-sm font-medium">Flash Options</h2>
            </div>
            <div className="p-4 flex flex-col gap-5">
              <div className="grid gap-2">
                <div className="flex items-center justify-between">
                  <Label className="text-sm">Timeout per operation</Label>
                  <span className="text-sm tabular-nums font-mono text-muted-foreground">{timeoutSec}s</span>
                </div>
                <Slider
                  min={1} max={30} step={1}
                  value={[timeoutSec]}
                  onValueChange={(v) => { const n = sliderVal(v); if (n !== undefined) setTimeoutSec(n); }}
                  disabled={flashState === "flashing"}
                />
              </div>
              <div className="grid gap-2">
                <div className="flex items-center justify-between">
                  <Label className="text-sm">Retry attempts</Label>
                  <span className="text-sm tabular-nums font-mono text-muted-foreground">{retries}</span>
                </div>
                <Slider
                  min={1} max={20} step={1}
                  value={[retries]}
                  onValueChange={(v) => { const n = sliderVal(v); if (n !== undefined) setRetries(n); }}
                  disabled={flashState === "flashing"}
                />
              </div>
            </div>
          </div>
        )}

        {/* ── Flash action + progress ─────────────────────────── */}
        {(developerMode || flashState !== "idle" || flashLog.length > 0) && (
        <div className="rounded-lg border bg-card">
          <div className="border-b px-4 py-3">
            <h2 className="text-sm font-medium">Flash</h2>
          </div>
          <div className="p-4 flex flex-col gap-4">
            <div className="flex items-center gap-3">
              {developerMode && (
                <Button
                  disabled={!canFlash}
                  className="gap-2"
                  onClick={() => setConfirmOpen(true)}
                >
                  <IconUpload className="size-4" />
                  Flash Firmware
                </Button>
              )}

              {flashState === "flashing" && (
                <span className="text-xs text-muted-foreground animate-pulse">Flashing… {flashProgress}%</span>
              )}
              {flashState === "success" && (
                <Badge variant="default" className="gap-1">
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
              <ScrollArea className="h-44 rounded-md border bg-muted/30 p-3">
                <pre className="text-xs font-mono whitespace-pre-wrap text-muted-foreground">
                  {flashLog.join("\n")}
                  <span ref={logEndRef} />
                </pre>
              </ScrollArea>
            )}
          </div>
        </div>
        )}

      </PageContent>

      {/* ── Confirm dialog ──────────────────────────────────── */}
      <Dialog open={confirmOpen} onOpenChange={setConfirmOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <IconAlertTriangle className="size-5 text-destructive" />
              Confirm Firmware Flash
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
              Flash Now
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
