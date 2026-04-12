import { useState, useCallback } from "react";
import { open as tauriOpen } from "@tauri-apps/plugin-dialog";
import { readFile } from "@tauri-apps/plugin-fs";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheFirmware, resolveFirmwareVersion, type FirmwareResolveResult } from "@/lib/kbhe/firmware";
import { formatFirmwareVersion } from "@/lib/kbhe/protocol";
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
  IconX,
  IconPlugConnected,
  IconPlugConnectedX,
} from "@tabler/icons-react";
import { cn } from "@/lib/utils";
import { PageContent } from "@/components/shared/PageLayout";

type FlashState = "idle" | "flashing" | "success" | "error";

export default function Firmware() {
  const { status, firmwareVersion } = useDeviceSession();
  const connected = status === "connected" || status === "updater";

  const [firmwareBytes, setFirmwareBytes] = useState<Uint8Array | null>(null);
  const [fileName, setFileName] = useState<string | null>(null);
  const [fileVersion, setFileVersion] = useState<FirmwareResolveResult | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [flashState, setFlashState] = useState<FlashState>("idle");
  const [flashProgress, setFlashProgress] = useState(0);
  const [flashLog, setFlashLog] = useState<string[]>([]);
  const [confirmOpen, setConfirmOpen] = useState(false);
  const [timeoutSec, setTimeoutSec] = useState(5);
  const [retries, setRetries] = useState(5);

  const appendLog = useCallback((msg: string) => {
    setFlashLog((prev) => [...prev, `[${new Date().toLocaleTimeString()}] ${msg}`]);
  }, []);

  const handleFilePick = useCallback(async () => {
    try {
      const selected = await tauriOpen({
        multiple: false,
        filters: [{ name: "Firmware", extensions: ["bin"] }],
      });
      if (!selected) return;

      const filePath = typeof selected === "string" ? selected : selected;

      setFileError(null);
      setFileVersion(null);
      setFlashState("idle");
      setFlashLog([]);
      setFlashProgress(0);

      const bytes = await readFile(filePath);
      setFirmwareBytes(new Uint8Array(bytes));
      const name = filePath.split(/[\\/]/).pop() ?? filePath;
      setFileName(name);

      try {
        const versionInfo = resolveFirmwareVersion(new Uint8Array(bytes));
        setFileVersion(versionInfo);
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setFileError(msg);
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setFileError(msg);
    }
  }, []);

  const handleFlash = useCallback(async () => {
    if (!firmwareBytes) return;
    setConfirmOpen(false);
    setFlashState("flashing");
    setFlashProgress(0);
    setFlashLog([]);

    try {
      const finalVersion = await kbheFirmware.flashFirmware(firmwareBytes, {
        timeoutMs: timeoutSec * 1000,
        retries,
        onLog: appendLog,
        onProgress: ({ percent }) => setFlashProgress(percent),
      });

      appendLog(`Flash complete! Version: ${formatFirmwareVersion(finalVersion)}`);
      setFlashState("success");

      appendLog("Reconnecting to runtime device...");
      await new Promise((r) => setTimeout(r, 2000));
      await DeviceSessionManager.reconnect();
      appendLog("Reconnected successfully.");
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      appendLog(`Error: ${msg}`);
      setFlashState("error");
    }
  }, [firmwareBytes, timeoutSec, retries, appendLog]);

  const fileSizeKb = firmwareBytes ? (firmwareBytes.length / 1024).toFixed(1) : null;
  const canFlash = connected && !!firmwareBytes && flashState !== "flashing";

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent containerClassName="max-w-3xl">

          {/* ── Device status strip ─────────────────────────────── */}
          <div className="flex items-center gap-3 rounded-lg border bg-card p-4">
            <div className={cn(
              "flex size-10 items-center justify-center rounded-full",
              connected ? "bg-green-500/10 text-green-600 dark:text-green-400" : "bg-muted text-muted-foreground",
            )}>
              {connected
                ? <IconPlugConnected className="size-5" />
                : <IconPlugConnectedX className="size-5" />}
            </div>
            <div className="flex flex-col gap-0.5 flex-1 min-w-0">
              <span className="text-sm font-medium">
                {status === "updater" ? "Updater Mode" : connected ? "Device Connected" : "No Device"}
              </span>
              <span className="text-xs text-muted-foreground truncate">
                {firmwareVersion
                  ? `Installed firmware: ${firmwareVersion}`
                  : connected
                    ? "Firmware version unknown"
                    : "Connect your keyboard to flash firmware"}
              </span>
            </div>
            {firmwareVersion && (
              <Badge variant="secondary" className="font-mono shrink-0">{firmwareVersion}</Badge>
            )}
          </div>

          {/* ── File picker area ────────────────────────────────── */}
          <div className="rounded-lg border bg-card">
            <div className="border-b px-4 py-3">
              <h2 className="text-sm font-medium">Firmware File</h2>
            </div>
            <div className="p-4 flex flex-col gap-4">
              {!fileName ? (
                <button
                  type="button"
                  onClick={() => void handleFilePick()}
                  className="flex flex-col items-center justify-center gap-3 rounded-lg border-2 border-dashed border-muted-foreground/25 py-10 text-muted-foreground hover:border-primary/40 hover:text-foreground transition-colors cursor-pointer"
                >
                  <IconFileUpload className="size-8" />
                  <div className="text-center">
                    <p className="text-sm font-medium">Select firmware binary</p>
                    <p className="text-xs mt-0.5">Choose a .bin file to flash</p>
                  </div>
                </button>
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
                </div>
              )}
            </div>
          </div>

          {/* ── Flash options ───────────────────────────────────── */}
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

          {/* ── Flash action + progress ─────────────────────────── */}
          <div className="rounded-lg border bg-card">
            <div className="border-b px-4 py-3">
              <h2 className="text-sm font-medium">Flash</h2>
            </div>
            <div className="p-4 flex flex-col gap-4">
              <div className="flex items-center gap-3">
                <Button
                  disabled={!canFlash}
                  className="gap-2"
                  onClick={() => setConfirmOpen(true)}
                >
                  <IconUpload className="size-4" />
                  Flash Firmware
                </Button>

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
                  </pre>
                </ScrollArea>
              )}
            </div>
          </div>

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
