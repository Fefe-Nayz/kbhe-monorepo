import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import {
  patchActiveAppProfileLedSnapshot,
  patchActiveAppProfileNkroEnabled,
  patchActiveAppProfileOptions,
} from "@/lib/kbhe/profile-snapshot-store";
import { queryKeys } from "@/lib/query/keys";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageContent } from "@/components/shared/PageLayout";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import { Input } from "@/components/ui/input";
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
import { useEffect, useState } from "react";
import { toast } from "sonner";

const KEYBOARD_NAME_LENGTH = 32;

function sanitizeKeyboardName(value: string): string {
  return Array.from(value)
    .filter((char) => {
      const code = char.charCodeAt(0);
      return code >= 0x20 && code <= 0x7e;
    })
    .join("")
    .slice(0, KEYBOARD_NAME_LENGTH);
}

export default function Device() {
  const { status, deviceInfo, firmwareVersion } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();

  const [resetOpen, setResetOpen] = useState(false);
  const [bootloaderOpen, setBootloaderOpen] = useState(false);
  const [keyboardNameInput, setKeyboardNameInput] = useState("");
  const [keyboardNameDirty, setKeyboardNameDirty] = useState(false);

  const deviceIdentityQ = useQuery({
    queryKey: queryKeys.device.identity(),
    queryFn: () => kbheDevice.getDeviceInfo(),
    enabled: connected,
  });

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
      const patchOptions = (patch: Partial<NonNullable<typeof optionsQ.data>>) => {
        if (optionsQ.data) {
          patchActiveAppProfileOptions({ ...optionsQ.data, ...patch });
        }
      };

      switch (key) {
        case "keyboard": {
          const ok = await kbheDevice.setKeyboardEnabled(value);
          if (ok) patchOptions({ keyboard_enabled: value });
          break;
        }
        case "gamepad": {
          const ok = await kbheDevice.setGamepadEnabled(value);
          if (ok) patchOptions({ gamepad_enabled: value });
          break;
        }
        case "nkro": {
          const ok = await kbheDevice.setNkroEnabled(value);
          if (ok) patchActiveAppProfileNkroEnabled(value);
          break;
        }
        case "led": {
          const ok = await kbheDevice.ledSetEnabled(value);
          if (ok) patchActiveAppProfileLedSnapshot({ enabled: value });
          break;
        }
        case "led_thermal_protection": {
          const ok = await kbheDevice.setLedThermalProtectionEnabled(value);
          if (ok) patchOptions({ led_thermal_protection_enabled: value });
          break;
        }
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

  const setKeyboardNameMutation = useMutation({
    mutationFn: async (nextName: string) => {
      const applied = await kbheDevice.setKeyboardName(nextName);
      if (applied === null) {
        throw new Error("Failed to update keyboard name on device.");
      }
      return applied;
    },
    onSuccess: (appliedName) => {
      setKeyboardNameInput(appliedName);
      setKeyboardNameDirty(false);
      void qc.invalidateQueries({ queryKey: queryKeys.device.identity() });
      toast.success("Keyboard name updated.");
    },
    onError: (error) => {
      const message = error instanceof Error ? error.message : "Failed to update keyboard name.";
      toast.error(message);
    },
  });

  const deviceKeyboardName = deviceIdentityQ.data?.keyboard_name ?? "";
  const serialNumber = deviceIdentityQ.data?.serial_number || deviceInfo?.serialNumber || "";

  useEffect(() => {
    if (!connected) {
      setKeyboardNameInput("");
      setKeyboardNameDirty(false);
      return;
    }
    if (!keyboardNameDirty) {
      setKeyboardNameInput(deviceKeyboardName);
    }
  }, [connected, deviceKeyboardName, keyboardNameDirty]);

  const identitySupported = connected && deviceIdentityQ.data !== null;
  const keyboardNameDisabled = !identitySupported || setKeyboardNameMutation.isPending;
  const keyboardEnabled = optionsQ.data?.keyboard_enabled ?? false;

  const handleKeyboardNameChange = (value: string) => {
    const sanitized = sanitizeKeyboardName(value);
    setKeyboardNameInput(sanitized);
    setKeyboardNameDirty(sanitized !== deviceKeyboardName);
  };

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
                <span className="text-sm text-muted-foreground">Serial:</span>
                {serialNumber ? (
                  <Badge variant="secondary" className="font-mono text-xs">{serialNumber}</Badge>
                ) : connected && deviceIdentityQ.isLoading ? (
                  <Skeleton className="h-5 w-40" />
                ) : (
                  <span className="text-sm text-muted-foreground">Unavailable</span>
                )}
              </div>
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
              <div className="flex flex-col gap-2 pt-1">
                <div className="flex items-center justify-between gap-3">
                  <span className="text-sm text-muted-foreground">Keyboard Name:</span>
                  <span className="text-xs text-muted-foreground">
                    {keyboardNameInput.length}/{KEYBOARD_NAME_LENGTH}
                  </span>
                </div>
                <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
                  <Input
                    value={keyboardNameInput}
                    maxLength={KEYBOARD_NAME_LENGTH}
                    disabled={keyboardNameDisabled}
                    placeholder="Custom keyboard name"
                    onChange={(event) => handleKeyboardNameChange(event.target.value)}
                    className="font-mono"
                  />
                  <div className="flex gap-2">
                    <Button
                      variant="outline"

                      disabled={keyboardNameDisabled || !keyboardNameDirty}
                      onClick={() => setKeyboardNameMutation.mutate(keyboardNameInput)}
                    >
                      Apply Name
                    </Button>
                    <Button
                      variant="ghost"

                      disabled={keyboardNameDisabled || !keyboardNameDirty}
                      onClick={() => {
                        setKeyboardNameInput(deviceKeyboardName);
                        setKeyboardNameDirty(false);
                      }}
                    >
                      Reset
                    </Button>
                  </div>
                </div>
                {connected && !identitySupported && !deviceIdentityQ.isLoading && (
                  <p className="text-xs text-muted-foreground">
                    This firmware does not expose device identity commands (0x2B/0x2C/0x2D).
                  </p>
                )}
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
              <div className="py-3 first:pt-0 last:pb-0">
                <FormRow label="Keyboard Enabled" className="py-0">
                  <Switch
                    checked={keyboardEnabled}
                    disabled={!connected}
                    onCheckedChange={(v) => toggleMutation.mutate({ key: "keyboard", value: v })}
                  />
                </FormRow>
                <div className="mt-3 ml-3 border-l pl-4">
                  <FormRow
                    label="NKRO Enabled"
                    description={keyboardEnabled
                      ? "N-Key Rollover for full anti-ghosting"
                      : "Enable Keyboard first to modify NKRO"
                    }
                    className="py-1"
                  >
                    <Switch
                      checked={nkroEnabledQ.data ?? false}
                      disabled={!connected || !keyboardEnabled}
                      onCheckedChange={(v) => toggleMutation.mutate({ key: "nkro", value: v })}
                    />
                  </FormRow>
                </div>
              </div>
              <FormRow label="Gamepad Enabled">
                <Switch checked={optionsQ.data?.gamepad_enabled ?? false} disabled={!connected}
                  onCheckedChange={(v) => toggleMutation.mutate({ key: "gamepad", value: v })} />
              </FormRow>
              <FormRow label="LED Enabled">
                <Switch checked={ledEnabledQ.data ?? false} disabled={!connected}
                  onCheckedChange={(v) => toggleMutation.mutate({ key: "led", value: v })} />
              </FormRow>
              <FormRow
                label="LED Thermal Protection"
                description="Limit LED brightness automatically when MCU temperature is high"
              >
                <Switch
                  checked={optionsQ.data?.led_thermal_protection_enabled ?? true}
                  disabled={!connected || !optionsQ.data}
                  onCheckedChange={(v) =>
                    toggleMutation.mutate({ key: "led_thermal_protection", value: v })
                  }
                />
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
