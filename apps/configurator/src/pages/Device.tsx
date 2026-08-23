import { type ReactNode } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { requireDeviceSuccess } from "@/lib/kbhe/mutation-result";
import {
  patchActiveAppProfileLedSnapshot,
  patchActiveAppProfileNkroEnabled,
  patchActiveAppProfileOptions,
} from "@/lib/kbhe/profile-snapshot-store";
import { queryKeys } from "@/lib/query/keys";
import { SectionCard, FormRow, FormRows } from "@/components/shared/SectionCard";
import { PageContent, PageSection } from "@/components/shared/PageLayout";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
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
  IconPlugConnected,
  IconDatabaseExport,
  IconAlertTriangle,
  IconRotateClockwise2,
  IconTag,
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
          patchActiveAppProfileOptions(patch);
        }
      };

      switch (key) {
        case "keyboard": {
          const ok = await kbheDevice.setKeyboardEnabled(value);
          requireDeviceSuccess(ok, "keyboard output setting");
          patchOptions({ keyboard_enabled: value });
          break;
        }
        case "gamepad": {
          const ok = await kbheDevice.setGamepadEnabled(value);
          requireDeviceSuccess(ok, "gamepad output setting");
          patchOptions({ gamepad_enabled: value });
          break;
        }
        case "nkro": {
          const ok = await kbheDevice.setNkroEnabled(value);
          requireDeviceSuccess(ok, "NKRO setting");
          patchActiveAppProfileNkroEnabled(value);
          break;
        }
        case "led": {
          const ok = await kbheDevice.ledSetEnabled(value);
          requireDeviceSuccess(ok, "LED setting");
          patchActiveAppProfileLedSnapshot({ enabled: value });
          break;
        }
        case "led_thermal_protection": {
          const ok = await kbheDevice.setLedThermalProtectionEnabled(value);
          requireDeviceSuccess(ok, "LED thermal protection setting");
          patchOptions({ led_thermal_protection_enabled: value });
          break;
        }
      }
    },
    onSuccess: () => void qc.invalidateQueries(),
    onError: (error) => {
      toast.error(error instanceof Error ? error.message : "Failed to update the device setting.");
    },
  });

  const saveMutation = useMutation({
    mutationFn: async () => {
      const ok = await kbheDevice.saveSettings();
      requireDeviceSuccess(ok, "saving settings to flash");
    },
    onSuccess: () => toast.success("Settings saved to device flash."),
    onError: (error) => {
      toast.error(error instanceof Error ? error.message : "Failed to save settings to flash.");
    },
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
    <PageContent containerClassName="max-w-5xl">
      <PageSection title="Identity">
        <div className="grid grid-cols-1 gap-4 lg:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]">
          <SectionCard
            title="Connection"
            description="What the app currently sees on the USB bus."
            icon={<IconPlugConnected />}
            footer={
              <Button
                variant="outline"
                size="sm"
                onClick={() => void DeviceSessionManager.reconnect()}
              >
                <IconRefresh className="size-4" />
                Reconnect
              </Button>
            }
          >
            <dl className="flex flex-col">
              <DetailRow label="Device" value={deviceInfo?.product ?? "Not connected"} />
              <DetailRow
                label="Status"
                value={
                  <span
                    className={cn(
                      "inline-flex items-center gap-1.5 font-medium capitalize",
                      connected ? "text-success" : "text-muted-foreground",
                    )}
                  >
                    <span
                      className={cn(
                        "size-1.5 rounded-full",
                        connected ? "bg-success" : "bg-muted-foreground/50",
                      )}
                    />
                    {status}
                  </span>
                }
              />
              {deviceInfo && (
                <DetailRow
                  label="VID:PID"
                  mono
                  value={`${deviceInfo.vid.toString(16).padStart(4, "0")}:${deviceInfo.pid
                    .toString(16)
                    .padStart(4, "0")}`}
                />
              )}
              <DetailRow
                label="Serial"
                mono
                value={
                  serialNumber ??
                  (connected && deviceIdentityQ.isLoading ? (
                    <Skeleton className="h-4 w-36" />
                  ) : (
                    "Unavailable"
                  ))
                }
              />
              <DetailRow
                label="Firmware"
                mono
                value={firmwareVersion ?? <Skeleton className="h-4 w-14" />}
              />
            </dl>
          </SectionCard>

          <SectionCard
            title="Keyboard name"
            description="Shown in the sidebar and reported to the host over USB."
            icon={<IconTag />}
            headerRight={
              <span className="font-mono text-xs tabular-nums text-muted-foreground">
                {keyboardNameInput.length}/{KEYBOARD_NAME_LENGTH}
              </span>
            }
            footer={
              <>
                <Button
                  variant="ghost"
                  size="sm"
                  disabled={keyboardNameDisabled || !keyboardNameDirty}
                  onClick={() => {
                    setKeyboardNameInput(deviceKeyboardName);
                    setKeyboardNameDirty(false);
                  }}
                >
                  Discard
                </Button>
                <Button
                  size="sm"
                  disabled={keyboardNameDisabled || !keyboardNameDirty}
                  onClick={() => setKeyboardNameMutation.mutate(keyboardNameInput)}
                >
                  Apply name
                </Button>
              </>
            }
          >
            <div className="flex flex-col gap-2">
              <Input
                value={keyboardNameInput}
                maxLength={KEYBOARD_NAME_LENGTH}
                disabled={keyboardNameDisabled}
                placeholder="Custom keyboard name"
                onChange={(event) => handleKeyboardNameChange(event.target.value)}
                className="font-mono"
              />
              {connected && !identitySupported && !deviceIdentityQ.isLoading ? (
                <p className="text-xs leading-relaxed text-warning">
                  This firmware does not expose the device identity commands
                  (0x2B/0x2C/0x2D), so the name cannot be changed from here.
                </p>
              ) : (
                <p className="text-xs leading-relaxed text-muted-foreground">
                  Up to {KEYBOARD_NAME_LENGTH} characters. Takes effect after the next
                  USB re-enumeration.
                </p>
              )}
            </div>
          </SectionCard>
        </div>
      </PageSection>

      <PageSection
        title="Input modes"
        description="Which USB interfaces the keyboard exposes to the host."
      >
        <SectionCard>
          <FormRows>
            <FormRow
              label="Keyboard output"
              description="Send standard HID keystrokes. Turning this off silences the board."
            >
              <Switch
                checked={keyboardEnabled}
                disabled={!connected}
                onCheckedChange={(v) => toggleMutation.mutate({ key: "keyboard", value: v })}
              />
            </FormRow>
            <FormRow
              nested
              label="N-key rollover"
              description={
                keyboardEnabled
                  ? "Report every held key at once, instead of the standard six."
                  : "Enable keyboard output first."
              }
            >
              <Switch
                checked={nkroEnabledQ.data ?? false}
                disabled={!connected || !keyboardEnabled}
                onCheckedChange={(v) => toggleMutation.mutate({ key: "nkro", value: v })}
              />
            </FormRow>
            <FormRow
              label="Gamepad output"
              description="Expose an analog controller alongside the keyboard."
            >
              <Switch
                checked={optionsQ.data?.gamepad_enabled ?? false}
                disabled={!connected}
                onCheckedChange={(v) => toggleMutation.mutate({ key: "gamepad", value: v })}
              />
            </FormRow>
          </FormRows>
        </SectionCard>
      </PageSection>

      <PageSection title="Lighting hardware">
        <SectionCard>
          <FormRows>
            <FormRow
              label="LEDs powered"
              description="Master switch for the RGB matrix. Per-effect settings live on the Lighting page."
            >
              <Switch
                checked={ledEnabledQ.data ?? false}
                disabled={!connected}
                onCheckedChange={(v) => toggleMutation.mutate({ key: "led", value: v })}
              />
            </FormRow>
            <FormRow
              label="Thermal protection"
              description="Automatically dim the LEDs when the MCU runs hot."
            >
              <Switch
                checked={optionsQ.data?.led_thermal_protection_enabled ?? true}
                disabled={!connected || !optionsQ.data}
                onCheckedChange={(v) =>
                  toggleMutation.mutate({ key: "led_thermal_protection", value: v })
                }
              />
            </FormRow>
          </FormRows>
        </SectionCard>
      </PageSection>

      <PageSection title="Maintenance">
        <div className="grid grid-cols-1 gap-4 lg:grid-cols-[minmax(0,1.3fr)_minmax(0,1fr)]">
          <SectionCard
            title="Persistence"
            description="Settings live in RAM until they are written to flash."
            icon={<IconDatabaseExport />}
          >
            <div className="flex flex-wrap gap-2">
              <Button
                disabled={!connected || saveMutation.isPending}
                onClick={() => saveMutation.mutate()}
              >
                <IconDatabaseExport className="size-4" />
                Save to flash
              </Button>
              <Button
                variant="outline"
                disabled={!connected || rebootMutation.isPending}
                onClick={() => rebootMutation.mutate()}
              >
                <IconPower className="size-4" />
                Restart keyboard
              </Button>
            </div>
          </SectionCard>

          <SectionCard
            tone="danger"
            title="Danger zone"
            description="These actions cannot be undone."
            icon={<IconAlertTriangle />}
          >
            <div className="flex flex-col gap-2">
              <Dialog open={bootloaderOpen} onOpenChange={setBootloaderOpen}>
                <DialogTrigger render={
                  <Button variant="outline" size="sm" className="justify-start" disabled={!connected}>
                    <IconRotateClockwise2 className="size-4" />
                    Enter bootloader (DFU)
                  </Button>
                } />
                <DialogContent>
                  <DialogHeader>
                    <DialogTitle>Enter bootloader?</DialogTitle>
                    <DialogDescription>
                      The keyboard reboots into firmware update mode and stops working as a
                      keyboard until it is reflashed or power-cycled.
                    </DialogDescription>
                  </DialogHeader>
                  <DialogFooter>
                    <Button variant="outline" onClick={() => setBootloaderOpen(false)}>Cancel</Button>
                    <Button
                      variant="destructive"
                      onClick={() => { setBootloaderOpen(false); enterBootloaderMutation.mutate(); }}
                    >
                      Enter bootloader
                    </Button>
                  </DialogFooter>
                </DialogContent>
              </Dialog>

              <Dialog open={resetOpen} onOpenChange={setResetOpen}>
                <DialogTrigger render={
                  <Button variant="destructive" size="sm" className="justify-start" disabled={!connected}>
                    <IconAlertTriangle className="size-4" />
                    Factory reset
                  </Button>
                } />
                <DialogContent>
                  <DialogHeader>
                    <DialogTitle className="flex items-center gap-2">
                      <IconAlertTriangle className="size-5 text-destructive" />
                      Factory reset?
                    </DialogTitle>
                    <DialogDescription>
                      This erases every setting on the keyboard — calibration, keymaps,
                      profiles and gamepad configuration. It cannot be undone.
                    </DialogDescription>
                  </DialogHeader>
                  <DialogFooter>
                    <Button variant="outline" onClick={() => setResetOpen(false)}>Cancel</Button>
                    <Button
                      variant="destructive"
                      onClick={() => { setResetOpen(false); factoryResetMutation.mutate(); }}
                    >
                      Erase everything
                    </Button>
                  </DialogFooter>
                </DialogContent>
              </Dialog>
            </div>
          </SectionCard>
        </div>
      </PageSection>
    </PageContent>
  );
}

/** Label/value line for the identity cards. */
function DetailRow({
  label,
  value,
  mono,
}: {
  label: string;
  value: ReactNode;
  mono?: boolean;
}) {
  return (
    <div className="flex items-center justify-between gap-4 border-b border-border/50 py-2 last:border-b-0">
      <dt className="shrink-0 text-xs text-muted-foreground">{label}</dt>
      <dd className={cn("min-w-0 truncate text-sm", mono && "font-mono text-xs")}>{value}</dd>
    </div>
  );
}
