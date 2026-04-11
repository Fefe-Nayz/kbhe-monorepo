import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Switch } from "@/components/ui/switch";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import {
  AlertDialog, AlertDialogAction, AlertDialogCancel,
  AlertDialogContent, AlertDialogDescription, AlertDialogFooter,
  AlertDialogHeader, AlertDialogTitle, AlertDialogTrigger,
} from "@/components/ui/alert-dialog";

export default function Device() {
  const { status, developerMode, setDeveloperMode } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();
  const [rebootPending, setRebootPending] = useState(false);
  const [resetPending, setResetPending] = useState(false);

  const firmwareQ = useQuery({
    queryKey: ["device", "firmware"],
    queryFn: () => kbheDevice.getFirmwareVersion(),
    enabled: connected,
  });

  const optionsQ = useQuery({
    queryKey: queryKeys.device.options(),
    queryFn: () => kbheDevice.getOptions(),
    enabled: connected,
  });

  const nkroQ = useQuery({
    queryKey: queryKeys.device.nkroEnabled(),
    queryFn: () => kbheDevice.getNkroEnabled(),
    enabled: connected,
  });

  const mcuQ = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected,
    refetchInterval: connected ? 2000 : false,
  });

  const kbEnabledMut = useMutation({
    mutationFn: async (v: boolean) => { markSaving(); await kbheDevice.setKeyboardEnabled(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.device.options() }); },
    onError: markError,
  });

  const gpEnabledMut = useMutation({
    mutationFn: async (v: boolean) => { markSaving(); await kbheDevice.setGamepadEnabled(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.device.options() }); },
    onError: markError,
  });

  const nkroMut = useMutation({
    mutationFn: async (v: boolean) => { markSaving(); await kbheDevice.setNkroEnabled(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.device.nkroEnabled() }); },
    onError: markError,
  });

  const saveMut = useMutation({
    mutationFn: async () => { markSaving(); await kbheDevice.saveSettings(); },
    onSuccess: markSaved,
    onError: markError,
  });

  const rebootMut = useMutation({
    mutationFn: async () => {
      setRebootPending(true);
      await kbheDevice.reboot();
    },
    onSuccess: () => { setRebootPending(false); void DeviceSessionManager.reconnect(); },
    onError: () => setRebootPending(false),
  });

  const bootloaderMut = useMutation({
    mutationFn: () => kbheDevice.enterBootloader(),
  });

  const factoryResetMut = useMutation({
    mutationFn: async () => {
      setResetPending(true);
      await kbheDevice.factoryReset();
    },
    onSuccess: () => { setResetPending(false); void qc.invalidateQueries(); },
    onError: () => setResetPending(false),
  });

  const opts = optionsQ.data;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Device" description="Firmware, options, and device management" />
        <AutosaveStatus state={saveState} />
      </div>

      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs defaultValue="info" className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4">
            <TabsList className="h-9 mt-1">
              <TabsTrigger value="info">Info</TabsTrigger>
              <TabsTrigger value="options">Options</TabsTrigger>
              <TabsTrigger value="management">Management</TabsTrigger>
            </TabsList>
          </div>

          {/* Info tab */}
          <TabsContent value="info" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">

              <SectionCard title="Firmware">
                <div className="flex flex-col divide-y">
                  <FormRow label="Version">
                    {firmwareQ.isLoading ? (
                      <Skeleton className="h-5 w-32" />
                    ) : (
                      <Badge variant="secondary" className="font-mono text-xs">
                        {firmwareQ.data ?? "—"}
                      </Badge>
                    )}
                  </FormRow>
                  <FormRow label="Connection Status">
                    <Badge variant={connected ? "default" : "outline"}>
                      {status}
                    </Badge>
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="MCU Metrics" description="Live device performance">
                {mcuQ.isLoading ? (
                  <div className="space-y-2">{[0,1,2,3].map(i => <Skeleton key={i} className="h-5 w-full" />)}</div>
                ) : !mcuQ.data ? (
                  <p className="text-sm text-muted-foreground">Connect device to view metrics.</p>
                ) : (
                  <div className="grid grid-cols-2 gap-x-8 gap-y-2 text-sm">
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Temperature</span>
                      <span className="tabular-nums font-mono text-xs">
                        {mcuQ.data.temperature_valid && mcuQ.data.temperature_c != null
                          ? `${mcuQ.data.temperature_c.toFixed(1)} °C`
                          : "—"}
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Vref</span>
                      <span className="tabular-nums font-mono text-xs">{mcuQ.data.vref_mv} mV</span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Core clock</span>
                      <span className="tabular-nums font-mono text-xs">
                        {(mcuQ.data.core_clock_hz / 1_000_000).toFixed(0)} MHz
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Scan rate</span>
                      <span className="tabular-nums font-mono text-xs">
                        {mcuQ.data.scan_rate_hz.toFixed(0)} Hz
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Scan cycle</span>
                      <span className="tabular-nums font-mono text-xs">
                        {mcuQ.data.scan_cycle_us} µs
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">CPU load</span>
                      <span className="tabular-nums font-mono text-xs">
                        {mcuQ.data.load_percent.toFixed(1)} %
                      </span>
                    </div>
                  </div>
                )}
              </SectionCard>

            </div>
          </TabsContent>

          {/* Options tab */}
          <TabsContent value="options" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">

              <SectionCard title="HID Interfaces">
                <div className="flex flex-col divide-y">
                  <FormRow label="Keyboard Enabled" description="Send keyboard HID reports">
                    {optionsQ.isLoading ? <Skeleton className="h-6 w-10" /> : (
                      <Switch
                        checked={opts?.keyboard_enabled ?? true}
                        disabled={!connected}
                        onCheckedChange={v => kbEnabledMut.mutate(v)}
                      />
                    )}
                  </FormRow>
                  <FormRow label="Gamepad Enabled" description="Send gamepad HID reports">
                    {optionsQ.isLoading ? <Skeleton className="h-6 w-10" /> : (
                      <Switch
                        checked={opts?.gamepad_enabled ?? false}
                        disabled={!connected}
                        onCheckedChange={v => gpEnabledMut.mutate(v)}
                      />
                    )}
                  </FormRow>
                  <FormRow label="NKRO" description="N-key rollover (requires USB re-enumeration)">
                    {nkroQ.isLoading ? <Skeleton className="h-6 w-10" /> : (
                      <Switch
                        checked={nkroQ.data ?? false}
                        disabled={!connected}
                        onCheckedChange={v => nkroMut.mutate(v)}
                      />
                    )}
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Persist Settings" description="Write current configuration to flash">
                <div className="flex items-center gap-3">
                  <Button
                    variant="default"
                    size="sm"
                    disabled={!connected || saveMut.isPending}
                    onClick={() => saveMut.mutate()}
                  >
                    {saveMut.isPending ? "Saving…" : "Save to Flash"}
                  </Button>
                  <p className="text-xs text-muted-foreground">
                    Settings are applied immediately but lost on reboot unless saved.
                  </p>
                </div>
              </SectionCard>

            </div>
          </TabsContent>

          {/* Management tab */}
          <TabsContent value="management" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">

              <SectionCard title="Application">
                <div className="flex flex-col divide-y">
                  <FormRow
                    label="Developer Mode"
                    description="Shows the Diagnostics page in the sidebar and enables developer tools."
                  >
                    <Switch
                      checked={developerMode}
                      onCheckedChange={setDeveloperMode}
                    />
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Reboot">
                <div className="flex items-center gap-3">
                  <Button
                    variant="outline"
                    size="sm"
                    disabled={!connected || rebootPending}
                    onClick={() => rebootMut.mutate()}
                  >
                    {rebootPending ? "Rebooting…" : "Reboot Device"}
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    disabled={!connected || bootloaderMut.isPending}
                    onClick={() => bootloaderMut.mutate()}
                  >
                    Enter Bootloader
                  </Button>
                </div>
              </SectionCard>

              <SectionCard
                title="Factory Reset"
                description="Erase all settings and restore firmware defaults"
              >
                <AlertDialog>
                  <AlertDialogTrigger
                    render={
                      <Button variant="destructive" size="sm" disabled={!connected || resetPending}>
                        {resetPending ? "Resetting…" : "Factory Reset"}
                      </Button>
                    }
                  />
                  <AlertDialogContent>
                    <AlertDialogHeader>
                      <AlertDialogTitle>Factory Reset?</AlertDialogTitle>
                      <AlertDialogDescription>
                        All key settings, calibration, lighting, and gamepad configuration will be
                        permanently erased and reset to firmware defaults. This cannot be undone.
                      </AlertDialogDescription>
                    </AlertDialogHeader>
                    <AlertDialogFooter>
                      <AlertDialogCancel>Cancel</AlertDialogCancel>
                      <AlertDialogAction
                        className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
                        onClick={() => factoryResetMut.mutate()}
                      >
                        Reset
                      </AlertDialogAction>
                    </AlertDialogFooter>
                  </AlertDialogContent>
                </AlertDialog>
              </SectionCard>

            </div>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
