import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageContent } from "@/components/shared/PageLayout";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import {
  IconRefresh,
  IconPower,
  IconDatabaseExport,
  IconAlertTriangle,
  IconRotateClockwise2,
} from "@tabler/icons-react";
import { useState } from "react";

export default function Device() {
  const { status, deviceInfo, firmwareVersion, setDeveloperMode, developerMode } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();

  const [resetOpen, setResetOpen] = useState(false);
  const [bootloaderOpen, setBootloaderOpen] = useState(false);

  const optionsQ = useQuery({
    queryKey: queryKeys.device.options(),
    queryFn: () => kbheDevice.getOptions(),
    enabled: connected,
  });

  const nkroEnabledQ = useQuery({
    queryKey: queryKeys.device.nkroEnabled(),
    queryFn: () => kbheDevice.getNkroEnabled(),
    enabled: connected,
  });

  const ledEnabledQ = useQuery({
    queryKey: queryKeys.led.enabled(),
    queryFn: () => kbheDevice.ledGetEnabled(),
    enabled: connected,
  });

  const toggleMutation = useMutation({
    mutationFn: async ({ key, value }: { key: string; value: boolean }) => {
      switch (key) {
        case "keyboard": await kbheDevice.setKeyboardEnabled(value); break;
        case "gamepad": await kbheDevice.setGamepadEnabled(value); break;
        case "nkro": await kbheDevice.setNkroEnabled(value); break;
        case "led": await kbheDevice.ledSetEnabled(value); break;
      }
    },
    onSuccess: () => void qc.invalidateQueries(),
  });

  const saveMutation = useMutation({
    mutationFn: () => kbheDevice.saveSettings(),
  });

  const rebootMutation = useMutation({
    mutationFn: async () => {
      await kbheDevice.reboot();
      await new Promise((r) => setTimeout(r, 2000));
      await DeviceSessionManager.reconnect();
    },
  });

  const factoryResetMutation = useMutation({
    mutationFn: async () => {
      await kbheDevice.factoryReset();
      await new Promise((r) => setTimeout(r, 3000));
      await DeviceSessionManager.reconnect();
    },
  });

  const enterBootloaderMutation = useMutation({
    mutationFn: async () => {
      await kbheDevice.enterBootloader();
      await new Promise((r) => setTimeout(r, 2000));
      await DeviceSessionManager.reconnect();
    },
  });

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent>

          <SectionCard title="Connection">
            <div className="flex flex-col gap-3">
              <div className="flex items-center gap-3">
                <span className="text-sm text-muted-foreground">Device:</span>
                <span className="text-sm font-medium">{deviceInfo?.product ?? "Not connected"}</span>
              </div>
              {deviceInfo && (
                <div className="flex items-center gap-3">
                  <span className="text-sm text-muted-foreground">VID:PID:</span>
                  <Badge variant="secondary" className="font-mono text-xs">
                    {deviceInfo.vid.toString(16).padStart(4, "0")}:{deviceInfo.pid.toString(16).padStart(4, "0")}
                  </Badge>
                </div>
              )}
              <div className="flex items-center gap-3">
                <span className="text-sm text-muted-foreground">Firmware:</span>
                {firmwareVersion ? (
                  <Badge variant="secondary" className="font-mono">{firmwareVersion}</Badge>
                ) : (
                  <Skeleton className="h-5 w-16" />
                )}
              </div>
              <div className="flex items-center gap-3">
                <span className="text-sm text-muted-foreground">Status:</span>
                <Badge variant={connected ? "default" : "secondary"}>{status}</Badge>
              </div>
              <Button variant="outline" size="sm" className="w-fit gap-1.5"
                onClick={() => void DeviceSessionManager.reconnect()}>
                <IconRefresh className="size-4" />
                Reconnect
              </Button>
            </div>
          </SectionCard>

          <SectionCard title="Options">
            <div className="flex flex-col divide-y">
              <FormRow label="Keyboard Enabled">
                <Switch checked={optionsQ.data?.keyboard_enabled ?? false} disabled={!connected}
                  onCheckedChange={(v) => toggleMutation.mutate({ key: "keyboard", value: v })} />
              </FormRow>
              <FormRow label="Gamepad Enabled">
                <Switch checked={optionsQ.data?.gamepad_enabled ?? false} disabled={!connected}
                  onCheckedChange={(v) => toggleMutation.mutate({ key: "gamepad", value: v })} />
              </FormRow>
              <FormRow label="NKRO Enabled" description="N-Key Rollover for full anti-ghosting">
                <Switch checked={nkroEnabledQ.data ?? false} disabled={!connected}
                  onCheckedChange={(v) => toggleMutation.mutate({ key: "nkro", value: v })} />
              </FormRow>
              <FormRow label="LED Enabled">
                <Switch checked={ledEnabledQ.data ?? false} disabled={!connected}
                  onCheckedChange={(v) => toggleMutation.mutate({ key: "led", value: v })} />
              </FormRow>
              <FormRow label="Developer Mode" description="Enables Diagnostics page">
                <Switch checked={developerMode}
                  onCheckedChange={(v) => setDeveloperMode(v)} />
              </FormRow>
            </div>
          </SectionCard>

          <SectionCard title="Actions">
            <div className="flex flex-wrap gap-3">
              <Button
                variant="default"
                size="sm"
                className="gap-1.5"
                disabled={!connected || saveMutation.isPending}
                onClick={() => saveMutation.mutate()}
              >
                <IconDatabaseExport className="size-4" />
                Save to Flash
              </Button>

              <Button
                variant="outline"
                size="sm"
                className="gap-1.5"
                disabled={!connected || rebootMutation.isPending}
                onClick={() => rebootMutation.mutate()}
              >
                <IconPower className="size-4" />
                Restart Keyboard
              </Button>

              <Dialog open={bootloaderOpen} onOpenChange={setBootloaderOpen}>
                <DialogTrigger render={
                  <Button variant="outline" size="sm" className="gap-1.5" disabled={!connected}>
                    <IconRotateClockwise2 className="size-4" />
                    Enter Bootloader
                  </Button>
                } />
                <DialogContent>
                  <DialogHeader>
                    <DialogTitle>Enter Bootloader</DialogTitle>
                    <DialogDescription>
                      The keyboard will reboot into firmware update (DFU) mode. It will be unavailable until reflashed or rebooted.
                    </DialogDescription>
                  </DialogHeader>
                  <DialogFooter>
                    <Button variant="outline" onClick={() => setBootloaderOpen(false)}>Cancel</Button>
                    <Button variant="destructive" onClick={() => { setBootloaderOpen(false); enterBootloaderMutation.mutate(); }}>
                      Confirm
                    </Button>
                  </DialogFooter>
                </DialogContent>
              </Dialog>

              <Dialog open={resetOpen} onOpenChange={setResetOpen}>
                <DialogTrigger render={
                  <Button variant="destructive" size="sm" className="gap-1.5" disabled={!connected}>
                    <IconAlertTriangle className="size-4" />
                    Factory Reset
                  </Button>
                } />
                <DialogContent>
                  <DialogHeader>
                    <DialogTitle className="flex items-center gap-2">
                      <IconAlertTriangle className="size-5 text-destructive" />
                      Factory Reset
                    </DialogTitle>
                    <DialogDescription>
                      This will erase ALL settings including calibration, keymaps, profiles, and gamepad configs. This action cannot be undone.
                    </DialogDescription>
                  </DialogHeader>
                  <DialogFooter>
                    <Button variant="outline" onClick={() => setResetOpen(false)}>Cancel</Button>
                    <Button variant="destructive" onClick={() => { setResetOpen(false); factoryResetMutation.mutate(); }}>
                      Reset Everything
                    </Button>
                  </DialogFooter>
                </DialogContent>
              </Dialog>
            </div>
          </SectionCard>
      </PageContent>
    </div>
  );
}
