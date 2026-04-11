import { useCallback, useEffect, useMemo, useState } from "react";
import {
  IconBolt,
  IconDeviceFloppy,
  IconPlugConnected,
  IconPlugConnectedX,
  IconRefresh,
  IconTerminal2,
  IconTrash,
} from "@tabler/icons-react";

import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { kbheDevice, type KbheTransportDeviceInfo } from "@/lib/kbhe";

interface DeviceSnapshot {
  firmwareVersion: string | null;
  keyboardEnabled: boolean | null;
  gamepadEnabled: boolean | null;
  rawHidEcho: boolean | null;
  nkroEnabled: boolean | null;
}

const emptySnapshot: DeviceSnapshot = {
  firmwareVersion: null,
  keyboardEnabled: null,
  gamepadEnabled: null,
  rawHidEcho: null,
  nkroEnabled: null,
};

export default function Settings() {
  const [devices, setDevices] = useState<KbheTransportDeviceInfo[]>([]);
  const [selectedPath, setSelectedPath] = useState<string>("");
  const [status, setStatus] = useState<string>("Idle");
  const [loading, setLoading] = useState(false);
  const [snapshot, setSnapshot] = useState<DeviceSnapshot>(emptySnapshot);
  const [connectedPath, setConnectedPath] = useState<string | null>(null);

  const runtimeDevices = useMemo(
    () => devices.filter((device) => device.kind === "runtime"),
    [devices],
  );

  const refreshDevices = useCallback(async () => {
    setLoading(true);
    try {
      const listed = await kbheDevice.listDevices();
      setDevices(listed);
      const currentState = await kbheDevice.connectionState();
      setConnectedPath(currentState.path);
      if (!selectedPath && listed.length > 0) {
        setSelectedPath(listed[0].path);
      }
      setStatus(`Found ${listed.length} KBHE HID interface(s).`);
    } catch (error) {
      setStatus(`Failed to enumerate devices: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setLoading(false);
    }
  }, [selectedPath]);

  const loadSnapshot = useCallback(async () => {
    try {
      const [firmwareVersion, options, nkroEnabled] = await Promise.all([
        kbheDevice.getFirmwareVersion(),
        kbheDevice.getOptions(),
        kbheDevice.getNkroEnabled(),
      ]);

      setSnapshot({
        firmwareVersion,
        keyboardEnabled: options?.keyboard_enabled ?? null,
        gamepadEnabled: options?.gamepad_enabled ?? null,
        rawHidEcho: options?.raw_hid_echo ?? null,
        nkroEnabled,
      });
      setStatus("Device state loaded.");
    } catch (error) {
      setStatus(`Failed to load device state: ${error instanceof Error ? error.message : String(error)}`);
    }
  }, []);

  useEffect(() => {
    void refreshDevices();
  }, [refreshDevices]);

  const connectSelected = async () => {
    if (!selectedPath) {
      setStatus("No device selected.");
      return;
    }
    setLoading(true);
    try {
      await kbheDevice.connect(selectedPath);
      setConnectedPath(selectedPath);
      setStatus("Connected to KBHE runtime interface.");
      await loadSnapshot();
    } catch (error) {
      setStatus(`Connection failed: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setLoading(false);
    }
  };

  const disconnect = async () => {
    setLoading(true);
    try {
      await kbheDevice.disconnect();
      setConnectedPath(null);
      setSnapshot(emptySnapshot);
      setStatus("Disconnected.");
    } catch (error) {
      setStatus(`Disconnect failed: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setLoading(false);
    }
  };

  const updateKeyboardEnabled = async (enabled: boolean) => {
    await kbheDevice.setKeyboardEnabled(enabled);
    await loadSnapshot();
  };

  const updateGamepadEnabled = async (enabled: boolean) => {
    await kbheDevice.setGamepadEnabled(enabled);
    await loadSnapshot();
  };

  const updateNkroEnabled = async (enabled: boolean) => {
    await kbheDevice.setNkroEnabled(enabled);
    await loadSnapshot();
  };

  return (
    <div className="mx-auto flex max-w-4xl flex-col gap-6 p-6">
      <div className="rounded-xl border border-border bg-card p-6 shadow-sm">
        <div className="mb-4 flex items-start justify-between gap-4">
          <div>
            <h2 className="text-xl font-semibold tracking-tight">KBHE Transport</h2>
            <p className="text-sm text-muted-foreground">
              Tauri/HID foundation wired to the keyboard. The rest of the UI can now build on top
              of this device stack.
            </p>
          </div>
          <Button variant="outline" onClick={() => void refreshDevices()} disabled={loading}>
            <IconRefresh className="size-4" />
            Refresh
          </Button>
        </div>

        <div className="grid gap-4 md:grid-cols-[1fr_auto_auto]">
          <div className="space-y-2">
            <label className="text-sm font-medium leading-none">Runtime device</label>
            <select
              className="flex h-10 w-full rounded-md border border-input bg-background px-3 text-sm"
              value={selectedPath}
              onChange={(event) => setSelectedPath(event.target.value)}
            >
              {runtimeDevices.length === 0 ? (
                <option value="">No KBHE runtime device found</option>
              ) : (
                runtimeDevices.map((device) => (
                  <option key={device.path} value={device.path}>
                    {device.product ?? "KBHE"} · {device.serialNumber ?? device.path}
                  </option>
                ))
              )}
            </select>
          </div>

          <Button onClick={() => void connectSelected()} disabled={loading || !selectedPath}>
            <IconPlugConnected className="size-4" />
            Connect
          </Button>

          <Button variant="outline" onClick={() => void disconnect()} disabled={loading || !connectedPath}>
            <IconPlugConnectedX className="size-4" />
            Disconnect
          </Button>
        </div>

        <div className="mt-4 rounded-lg border border-dashed border-slate-300 bg-slate-50 px-4 py-3 text-sm text-slate-700">
          {status}
        </div>
      </div>

      <div className="grid gap-6 lg:grid-cols-2">
        <div className="rounded-xl border border-border bg-card p-6 shadow-sm">
          <div className="mb-4 flex items-center justify-between">
            <div>
              <h3 className="text-lg font-semibold">Device State</h3>
              <p className="text-sm text-muted-foreground">
                Basic runtime settings read through the new TS protocol layer.
              </p>
            </div>
            <Button variant="outline" onClick={() => void loadSnapshot()} disabled={!connectedPath || loading}>
              <IconRefresh className="size-4" />
              Reload
            </Button>
          </div>

          <div className="space-y-4">
            <div className="rounded-lg border border-slate-200 bg-slate-50 px-4 py-3 text-sm">
              <div className="font-medium">Firmware</div>
              <div className="text-muted-foreground">
                {snapshot.firmwareVersion ?? "Unavailable"}
              </div>
            </div>

            <div className="flex items-start justify-between gap-4">
              <div className="space-y-1">
                <label className="text-sm font-medium leading-none">Keyboard HID</label>
                <p className="text-sm text-muted-foreground">Enable or disable keyboard output.</p>
              </div>
              <Switch
                checked={Boolean(snapshot.keyboardEnabled)}
                disabled={!connectedPath}
                onCheckedChange={(checked) => void updateKeyboardEnabled(checked)}
              />
            </div>

            <div className="flex items-start justify-between gap-4">
              <div className="space-y-1">
                <label className="text-sm font-medium leading-none">Gamepad HID</label>
                <p className="text-sm text-muted-foreground">Enable or disable gamepad output.</p>
              </div>
              <Switch
                checked={Boolean(snapshot.gamepadEnabled)}
                disabled={!connectedPath}
                onCheckedChange={(checked) => void updateGamepadEnabled(checked)}
              />
            </div>

            <div className="flex items-start justify-between gap-4">
              <div className="space-y-1">
                <label className="text-sm font-medium leading-none">NKRO</label>
                <p className="text-sm text-muted-foreground">Switch between 6KRO and NKRO output.</p>
              </div>
              <Switch
                checked={Boolean(snapshot.nkroEnabled)}
                disabled={!connectedPath}
                onCheckedChange={(checked) => void updateNkroEnabled(checked)}
              />
            </div>

            <div className="rounded-lg border border-slate-200 bg-slate-50 px-4 py-3 text-sm">
              <div className="font-medium">Raw HID Echo</div>
              <div className="text-muted-foreground">
                {snapshot.rawHidEcho === null ? "Unavailable" : snapshot.rawHidEcho ? "Enabled" : "Disabled"}
              </div>
            </div>
          </div>
        </div>

        <div className="rounded-xl border border-border bg-card p-6 shadow-sm">
          <div className="mb-4">
            <h3 className="text-lg font-semibold">Device Actions</h3>
            <p className="text-sm text-muted-foreground">
              Representative runtime commands now routed through the new KBHE client.
            </p>
          </div>

          <div className="space-y-4">
            <Button
              variant="outline"
              className="w-full justify-start"
              disabled={!connectedPath}
              onClick={() => void kbheDevice.saveSettings()}
            >
              <IconDeviceFloppy className="size-4" />
              Flush Settings
            </Button>

            <Button
              variant="outline"
              className="w-full justify-start"
              disabled={!connectedPath}
              onClick={() => void kbheDevice.usbReenumerate()}
            >
              <IconRefresh className="size-4" />
              USB Re-enumerate
            </Button>

            <Button
              variant="outline"
              className="w-full justify-start"
              disabled={!connectedPath}
              onClick={() => void kbheDevice.reboot()}
            >
              <IconBolt className="size-4" />
              Reboot
            </Button>

            <Button
              variant="outline"
              className="w-full justify-start"
              disabled={!connectedPath}
              onClick={() => void kbheDevice.enterBootloader()}
            >
              <IconTerminal2 className="size-4" />
              Enter Bootloader
            </Button>

            <Button
              variant="destructive"
              className="w-full justify-start"
              disabled={!connectedPath}
              onClick={() => void kbheDevice.factoryReset()}
            >
              <IconTrash className="size-4" />
              Factory Reset
            </Button>
          </div>
        </div>
      </div>
    </div>
  );
}
