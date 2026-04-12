import { useEffect, useRef, useCallback, useMemo } from "react";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { AnalogCurveEditor, type CurvePoint } from "@/components/analog-curve";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Switch } from "@/components/ui/switch";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type GamepadSettings, type GamepadCurvePoint } from "@/lib/kbhe/device";
import {
  GAMEPAD_AXES,
  GAMEPAD_BUTTONS,
  GAMEPAD_DIRECTIONS,
  GAMEPAD_API_MODES,
  GAMEPAD_KEYBOARD_ROUTING,
  GAMEPAD_BUTTON_NAMES,
  GAMEPAD_AXIS_NAMES,
  GAMEPAD_DIRECTION_NAMES,
} from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { IconDeviceGamepad, IconDeviceGamepad2 } from "@tabler/icons-react";
import { cn, selectItems } from "@/lib/utils";
import { usePageVisible } from "@/hooks/use-page-visible";

const VIEW_W = 350;
const VIEW_H = 200;
const MAX_DISTANCE = 4.0;

function deviceCurveToEditor(pts: GamepadCurvePoint[]): CurvePoint[] {
  return pts.map((p) => ({
    x: (p.x_mm / MAX_DISTANCE) * VIEW_W,
    y: VIEW_H - (p.y / 255) * VIEW_H,
  }));
}

function editorCurveToDevice(pts: CurvePoint[]): GamepadCurvePoint[] {
  return pts.map((p) => {
    const x_mm = (p.x / VIEW_W) * MAX_DISTANCE;
    return {
      x_01mm: Math.round(x_mm * 100),
      x_mm,
      y: Math.round((1 - p.y / VIEW_H) * 255),
    };
  });
}

const BUTTON_GRID: { label: string; value: number }[] = Object.entries(GAMEPAD_BUTTONS)
  .filter(([, v]) => v !== 0)
  .map(([label, value]) => ({ label, value }));

export default function Gamepad() {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const visible = usePageVisible();
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10)
    : null;

  // ── Queries ──

  const gamepadQ = useQuery({
    queryKey: queryKeys.gamepad.settings(),
    queryFn: () => kbheDevice.getGamepadSettings(),
    enabled: connected,
  });

  const keyMapQ = useQuery({
    queryKey: queryKeys.gamepad.keyMap(keyIndex ?? -1),
    queryFn: () => (keyIndex != null ? kbheDevice.getKeyGamepadMap(keyIndex) : null),
    enabled: connected && keyIndex != null,
  });

  const keyStatesQ = useQuery({
    queryKey: queryKeys.diagnostics.keyStates(),
    queryFn: () => kbheDevice.getKeyStates(),
    enabled: connected && visible,
    refetchInterval: connected && visible ? 50 : false,
  });

  // ── Mutations ──

  const gamepadMutation = useMutation({
    mutationFn: async (patch: Partial<GamepadSettings>) => {
      markSaving();
      await kbheDevice.setGamepadSettings(patch);
    },
    onSuccess: () => {
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.gamepad.settings() });
    },
    onError: markError,
  });

  const keyMapMutation = useMutation({
    mutationFn: async ({
      axis,
      direction,
      button,
    }: {
      axis: number;
      direction: number;
      button: number;
    }) => {
      if (keyIndex == null) return;
      markSaving();
      await kbheDevice.setKeyGamepadMap(keyIndex, axis, direction, button);
    },
    onSuccess: () => {
      markSaved();
      if (keyIndex != null)
        void qc.invalidateQueries({ queryKey: queryKeys.gamepad.keyMap(keyIndex) });
    },
    onError: markError,
  });

  const gs = gamepadQ.data;
  const km = keyMapQ.data;

  // ── Assign button helper ──

  const assignButton = useCallback(
    (buttonValue: number) => {
      if (keyIndex == null || !km) return;
      keyMapMutation.mutate({ axis: km.axis, direction: km.direction, button: buttonValue });
    },
    [keyIndex, km, keyMapMutation],
  );

  // ── Stick preview ──

  const stickX = useRef(0);
  const stickY = useRef(0);

  useEffect(() => {
    const states = keyStatesQ.data;
    if (!states) return;
    const distances = states.distances_mm;
    let lx = 0;
    let ly = 0;
    for (let i = 0; i < distances.length; i++) {
      const d = Math.min(distances[i] / MAX_DISTANCE, 1);
      if (d > 0) {
        // Approximate: any key mapped to LS axes contributes
        // We just show aggregate distance as a simple preview
        lx = Math.max(lx, d);
        ly = Math.max(ly, d);
      }
    }
    stickX.current = lx;
    stickY.current = ly;
  }, [keyStatesQ.data]);

  // ── Curve ──

  const curvePoints: CurvePoint[] = gs ? deviceCurveToEditor(gs.curve_points) : [];

  // Live preview during drag: throttled runtime-only SET, no query cache update.
  const liveCurveUpdate = useThrottledCall(async (pts: CurvePoint[]) => {
    if (!gs) return;
    await kbheDevice.setGamepadSettings({ ...gs, curve_points: editorCurveToDevice(pts) });
  });

  const handleCurveChange = useCallback(
    (pts: CurvePoint[]) => {
      if (!gs) return;
      const devicePts = editorCurveToDevice(pts);
      gamepadMutation.mutate({ ...gs, curve_points: devicePts });
    },
    [gs, gamepadMutation],
  );

  // ── Render key overlay showing current gamepad mapping ──

  const keyLegendMap = useMemo(() => {
    if (!km || keyIndex == null) return undefined;

    const parts: string[] = [];
    if (km.button !== 0) parts.push(GAMEPAD_BUTTON_NAMES[km.button] ?? `B${km.button}`);
    if (km.axis !== 0) {
      const axisName = GAMEPAD_AXIS_NAMES[km.axis] ?? `A${km.axis}`;
      const dir = GAMEPAD_DIRECTION_NAMES[km.direction] ?? "+";
      parts.push(`${axisName} ${dir}`);
    }

    if (parts.length === 0) return undefined;

    return {
      [`key-${keyIndex}`]: parts.join(", "),
    };
  }, [km, keyIndex]);

  // ── XInput / HID toggle ──

  const isXInput = gs?.api_mode === GAMEPAD_API_MODES["XInput (Xbox Compatible)"];

  const menubar = (
    <>
      <div className="flex items-center gap-3">
        <div className="flex items-center gap-2">
          <span className="text-sm text-muted-foreground">HID</span>
          <Switch
            checked={isXInput}
            disabled={!connected || gamepadQ.isLoading}
            onCheckedChange={(v) => {
              if (!gs) return;
              gamepadMutation.mutate({
                ...gs,
                api_mode: v
                  ? GAMEPAD_API_MODES["XInput (Xbox Compatible)"]
                  : GAMEPAD_API_MODES["HID (DirectInput)"],
              });
            }}
          />
          <span className="text-sm text-muted-foreground">XInput</span>
        </div>
        <Badge variant="outline" className="text-xs">
          {isXInput ? "XInput" : "HID"}
        </Badge>
      </div>
      <AutosaveStatus state={saveState} />
    </>
  );

  // ── Main render ──

  return (
    <KeyboardEditor
      keyboard={
        <BaseKeyboard
          mode="single"
          onButtonClick={() => {}}
          showLayerSelector={false}
          showRotary={false}
          keyLegendMap={connected ? keyLegendMap : undefined}
          keyLegendClassName="text-[9px] leading-tight truncate text-primary"
        />
      }
      menubar={menubar}
    >
      <Tabs defaultValue="setup" className="flex flex-col gap-4">
        <TabsList className="w-fit">
          <TabsTrigger value="setup">
            <IconDeviceGamepad className="size-4 mr-1.5" />
            Setup
          </TabsTrigger>
          <TabsTrigger value="analog">
            <IconDeviceGamepad2 className="size-4 mr-1.5" />
            Analog
          </TabsTrigger>
        </TabsList>

        {/* ─── Setup Tab ─── */}
        <TabsContent value="setup" className="mt-0">
          <div className="grid grid-cols-1 lg:grid-cols-[1fr_320px] gap-4">
            {/* Left: gamepad button grid */}
            <SectionCard
              title={
                keyIndex != null
                  ? `Key ${keyIndex} — Assign Gamepad Button`
                  : "Gamepad Button Map"
              }
              description={
                keyIndex == null
                  ? "Select a key above, then click a button to assign"
                  : "Click a button to assign it to this key"
              }
            >
              {!connected ? (
                <p className="text-sm text-muted-foreground py-4">
                  Connect a device to configure gamepad mapping.
                </p>
              ) : keyIndex == null ? (
                <p className="text-sm text-muted-foreground py-4">
                  Select a key on the keyboard above.
                </p>
              ) : keyMapQ.isLoading ? (
                <div className="grid grid-cols-4 gap-2">
                  {Array.from({ length: 16 }, (_, i) => (
                    <Skeleton key={i} className="h-12" />
                  ))}
                </div>
              ) : (
                <div className="grid grid-cols-4 gap-2">
                  {BUTTON_GRID.map((btn) => {
                    const active = km?.button === btn.value;
                    return (
                      <button
                        key={btn.value}
                        type="button"
                        className={cn(
                          "h-12 rounded-lg border text-xs font-medium transition-colors",
                          "hover:bg-accent hover:text-accent-foreground",
                          "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring",
                          active && "bg-primary text-primary-foreground hover:bg-primary/90",
                          !active && "bg-card",
                        )}
                        onClick={() => assignButton(btn.value)}
                        disabled={!connected}
                      >
                        {btn.label}
                      </button>
                    );
                  })}
                  {/* None / clear button */}
                  <button
                    type="button"
                    className={cn(
                      "h-12 rounded-lg border text-xs font-medium transition-colors",
                      "hover:bg-accent hover:text-accent-foreground",
                      km?.button === 0 && "bg-muted text-muted-foreground",
                    )}
                    onClick={() => assignButton(0)}
                    disabled={!connected}
                  >
                    None
                  </button>
                </div>
              )}

              {/* Axis / Direction selectors when a key is selected */}
              {keyIndex != null && km && (
                <div className="mt-4 flex flex-col divide-y border-t pt-2">
                  <FormRow label="Axis" description="Analog axis for this key">
                    <Select
                      value={String(km.axis)}
                      disabled={!connected}
                      items={selectItems(GAMEPAD_AXES)}
                      onValueChange={(v) =>
                        keyMapMutation.mutate({
                          axis: Number(v),
                          direction: km.direction,
                          button: km.button,
                        })
                      }
                    >
                      <SelectTrigger className="w-40 h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(GAMEPAD_AXES).map(([name, val]) => (
                            <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="Direction" description="Axis direction">
                    <Select
                      value={String(km.direction)}
                      disabled={!connected}
                      items={selectItems(GAMEPAD_DIRECTIONS)}
                      onValueChange={(v) =>
                        keyMapMutation.mutate({
                          axis: km.axis,
                          direction: Number(v),
                          button: km.button,
                        })
                      }
                    >
                      <SelectTrigger className="w-28 h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(GAMEPAD_DIRECTIONS).map(([name, val]) => (
                            <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                </div>
              )}
            </SectionCard>

            {/* Right: global switches */}
            <div className="flex flex-col gap-4">
              <SectionCard title="Input Routing">
                <div className="flex flex-col divide-y">
                  <FormRow
                    label="Keyboard Routing"
                    description="How keyboard output behaves alongside gamepad"
                  >
                    {gamepadQ.isLoading ? (
                      <Skeleton className="h-8 w-36" />
                    ) : (
                      <Select
                        value={String(gs?.keyboard_routing ?? 1)}
                        disabled={!connected}
                        items={selectItems(GAMEPAD_KEYBOARD_ROUTING)}
                        onValueChange={(v) => {
                          if (!gs) return;
                          gamepadMutation.mutate({
                            ...gs,
                            keyboard_routing: Number(v),
                          });
                        }}
                      >
                        <SelectTrigger className="w-44 h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectGroup>
                            {Object.entries(GAMEPAD_KEYBOARD_ROUTING).map(([name, val]) => (
                              <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                            ))}
                          </SelectGroup>
                        </SelectContent>
                      </Select>
                    )}
                  </FormRow>
                </div>
              </SectionCard>

              <StickPreview
                connected={connected}
                distances={keyStatesQ.data?.distances_mm ?? null}
              />
            </div>
          </div>
        </TabsContent>

        {/* ─── Analog Tab ─── */}
        <TabsContent value="analog" className="mt-0">
          <div className="grid grid-cols-1 lg:grid-cols-[1fr_320px] gap-4">
            {/* Left: curve editor */}
            <SectionCard
              title="Analog Curve"
              description="Drag points to shape the distance-to-output mapping"
            >
              {!connected || gamepadQ.isLoading ? (
                <Skeleton className="h-[240px] w-full" />
              ) : !gs ? (
                <p className="text-sm text-muted-foreground py-4">
                  Connect device to edit analog curve.
                </p>
              ) : (
                <AnalogCurveEditor
                  points={curvePoints}
                  onLiveChange={liveCurveUpdate}
                  onChange={handleCurveChange}
                />
              )}
            </SectionCard>

            {/* Right: analog options */}
            <div className="flex flex-col gap-4">
              <SectionCard title="Stick Options">
                <div className="flex flex-col divide-y">
                  <FormRow
                    label="Square Joystick Mode"
                    description="Snap stick output to a square boundary"
                  >
                    <Switch
                      checked={gs?.square_mode ?? false}
                      disabled={!connected || gamepadQ.isLoading}
                      onCheckedChange={(v) => {
                        if (!gs) return;
                        gamepadMutation.mutate({ ...gs, square_mode: v });
                      }}
                    />
                  </FormRow>
                  <FormRow
                    label="Snappy Joystick"
                    description="Enhanced stick response curve"
                  >
                    <Switch
                      checked={gs?.reactive_stick ?? false}
                      disabled={!connected || gamepadQ.isLoading}
                      onCheckedChange={(v) => {
                        if (!gs) return;
                        gamepadMutation.mutate({
                          ...gs,
                          reactive_stick: v,
                          snappy_mode: v,
                        });
                      }}
                    />
                  </FormRow>
                </div>
              </SectionCard>

              <StickPreview
                connected={connected}
                distances={keyStatesQ.data?.distances_mm ?? null}
              />
            </div>
          </div>
        </TabsContent>
      </Tabs>
    </KeyboardEditor>
  );
}

// ── Live Stick Preview ──

function StickPreview({
  connected,
  distances,
}: {
  connected: boolean;
  distances: number[] | null;
}) {
  const r = 60;
  const cx = r + 8;
  const cy = r + 8;
  const size = (r + 8) * 2;

  let dotX = cx;
  let dotY = cy;

  if (distances && distances.length > 0) {
    let sumX = 0;
    let sumY = 0;
    let count = 0;
    for (let i = 0; i < distances.length; i++) {
      const d = distances[i];
      if (d > 0.01) {
        sumX += d;
        sumY += d;
        count++;
      }
    }
    if (count > 0) {
      const avgNorm = Math.min((sumX / count) / MAX_DISTANCE, 1);
      dotX = cx + avgNorm * r * 0.7;
      dotY = cy - avgNorm * r * 0.7;
    }
  }

  return (
    <SectionCard title="Stick Preview" description="Live analog stick position">
      {!connected ? (
        <p className="text-sm text-muted-foreground py-4">Connect device to see live preview.</p>
      ) : (
        <div className="flex items-center justify-center py-2">
          <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`}>
            <circle
              cx={cx}
              cy={cy}
              r={r}
              className="fill-muted/40 stroke-border"
              strokeWidth={1.5}
            />
            <line
              x1={cx - r}
              y1={cy}
              x2={cx + r}
              y2={cy}
              className="stroke-border"
              strokeWidth={0.5}
              strokeDasharray="3 3"
            />
            <line
              x1={cx}
              y1={cy - r}
              x2={cx}
              y2={cy + r}
              className="stroke-border"
              strokeWidth={0.5}
              strokeDasharray="3 3"
            />
            <circle
              cx={dotX}
              cy={dotY}
              r={6}
              className="fill-primary stroke-primary-foreground"
              strokeWidth={1.5}
            />
          </svg>
        </div>
      )}
    </SectionCard>
  );
}
