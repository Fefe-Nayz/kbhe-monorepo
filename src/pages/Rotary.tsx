import { useEffect, useMemo, useRef, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { invoke } from "@tauri-apps/api/core";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { usePageVisible } from "@/hooks/use-page-visible";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice, type RotaryEncoderSettings, type RotaryBinding } from "@/lib/kbhe/device";
import { isVolumeServiceRunning } from "@/lib/kbhe/volume-service";
import {
  ROTARY_ACTIONS, ROTARY_BUTTON_ACTIONS, ROTARY_RGB_BEHAVIORS, ROTARY_PROGRESS_STYLES,
  LED_EFFECT_NAMES, ROTARY_BINDING_MODES, ROTARY_BINDING_LAYER_MODES, LAYER_COUNT,
} from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageContent } from "@/components/shared/PageLayout";
import { CommitSlider } from "@/components/ui/commit-slider";
import { Switch } from "@/components/ui/switch";
import {
  Select, SelectContent, SelectGroup, SelectItem, SelectTrigger, SelectValue,
} from "@/components/ui/select";
import { Skeleton } from "@/components/ui/skeleton";
import { ColorPicker } from "@/components/color-picker";
import { Badge } from "@/components/ui/badge";
import { selectItems, selectItemsReverse } from "@/lib/utils";

const ROTARY_ACTION_NAMES = Object.fromEntries(
  Object.entries(ROTARY_ACTIONS).map(([k, v]) => [v, k]),
);
const ROTARY_BUTTON_ACTION_NAMES = Object.fromEntries(
  Object.entries(ROTARY_BUTTON_ACTIONS).map(([k, v]) => [v, k]),
);

const ROTARY_SETTINGS_POLL_MS = 500;
const ROTARY_PREVIEW_POLL_MS = 180;
const ROTARY_PREVIEW_SWEEP_DEG = 252;
const ROTARY_PREVIEW_START_DEG = -126;
const LED_EFFECT_MAX = Math.max(...Object.keys(LED_EFFECT_NAMES).map((key) => Number(key)));

function clampPercent(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(100, value));
}

function rgbToHueDeg(r: number, g: number, b: number): number {
  const nr = r / 255;
  const ng = g / 255;
  const nb = b / 255;
  const max = Math.max(nr, ng, nb);
  const min = Math.min(nr, ng, nb);
  const delta = max - min;
  if (delta === 0) return 0;

  let hue = 0;
  if (max === nr) {
    hue = ((ng - nb) / delta) % 6;
  } else if (max === ng) {
    hue = (nb - nr) / delta + 2;
  } else {
    hue = (nr - ng) / delta + 4;
  }

  hue *= 60;
  if (hue < 0) hue += 360;
  return hue;
}

async function getSystemVolumeLevel(): Promise<number | null> {
  try {
    return await invoke<number | null>("kbhe_get_system_volume");
  } catch {
    return null;
  }
}

async function getRotaryLivePercent(settings: RotaryEncoderSettings): Promise<number | null> {
  switch (settings.rotation_action) {
    case ROTARY_ACTIONS.Volume: {
      const volume = await getSystemVolumeLevel();
      return volume == null ? null : clampPercent((volume / 255) * 100);
    }

    case ROTARY_ACTIONS["LED Brightness"]: {
      const brightness = await kbheDevice.ledGetBrightness();
      return brightness == null ? null : clampPercent((brightness / 255) * 100);
    }

    case ROTARY_ACTIONS["Effect Speed"]: {
      const speed = await kbheDevice.getLedEffectSpeed();
      return speed == null ? null : clampPercent((speed / 255) * 100);
    }

    case ROTARY_ACTIONS["Effect Cycle"]: {
      const effect = await kbheDevice.getLedEffect();
      if (effect == null || LED_EFFECT_MAX <= 0) return null;
      return clampPercent((effect / LED_EFFECT_MAX) * 100);
    }

    case ROTARY_ACTIONS["RGB Customizer"]: {
      if (settings.rgb_behavior === ROTARY_RGB_BEHAVIORS.Hue) {
        const color = await kbheDevice.getLedEffectColor();
        if (!color) return null;
        const [r, g, b] = color;
        const hue = rgbToHueDeg(r, g, b);
        return clampPercent((hue / 360) * 100);
      }

      if (settings.rgb_behavior === ROTARY_RGB_BEHAVIORS.Brightness) {
        const color = await kbheDevice.getLedEffectColor();
        if (!color) return null;
        const [r, g, b] = color;
        const brightness = Math.max(r, g, b);
        return clampPercent((brightness / 255) * 100);
      }

      if (settings.rgb_behavior === ROTARY_RGB_BEHAVIORS["Effect Speed"]) {
        const speed = await kbheDevice.getLedEffectSpeed();
        return speed == null ? null : clampPercent((speed / 255) * 100);
      }

      if (settings.rgb_behavior === ROTARY_RGB_BEHAVIORS["Effect Cycle"]) {
        const effect = await kbheDevice.getLedEffect();
        if (effect == null || LED_EFFECT_MAX <= 0) return null;
        return clampPercent((effect / LED_EFFECT_MAX) * 100);
      }

      return null;
    }

    default:
      return null;
  }
}

function RotaryVisual({ settings, livePercent, isActive }: {
  settings: RotaryEncoderSettings;
  livePercent: number | null;
  isActive: boolean;
}) {
  const progressColor = settings.progress_color
    ? `rgb(${settings.progress_color[0]}, ${settings.progress_color[1]}, ${settings.progress_color[2]})`
    : "rgb(40, 210, 64)";

  const r = 56;
  const cx = 80;
  const cy = 80;
  const size = 160;
  const previewPercent = clampPercent(livePercent ?? 0);
  const activeSweep = (2 * Math.PI * (r - 8) * previewPercent) / 100;
  const totalSweep = 2 * Math.PI * (r - 8);
  const indicatorDeg = ROTARY_PREVIEW_START_DEG + (ROTARY_PREVIEW_SWEEP_DEG * previewPercent) / 100;
  const indicatorRad = (indicatorDeg * Math.PI) / 180;
  const indicatorInner = 10;
  const indicatorOuter = 18;
  const x1 = cx + Math.cos(indicatorRad) * indicatorInner;
  const y1 = cy + Math.sin(indicatorRad) * indicatorInner;
  const x2 = cx + Math.cos(indicatorRad) * indicatorOuter;
  const y2 = cy + Math.sin(indicatorRad) * indicatorOuter;

  return (
    <div className="flex flex-col items-center gap-3">
      <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`}>
        <circle
          cx={cx} cy={cy} r={r}
          className="fill-muted/40 stroke-border"
          strokeWidth={2}
        />
        <circle
          cx={cx} cy={cy} r={r - 8}
          fill="none"
          stroke={progressColor}
          strokeWidth={4}
          strokeDasharray={`${activeSweep} ${Math.max(0, totalSweep - activeSweep)}`}
          strokeLinecap="round"
          transform={`rotate(-126 ${cx} ${cy})`}
          opacity={isActive ? 0.95 : 0.65}
          className="transition-all duration-200"
        />
        <circle
          cx={cx} cy={cy} r={20}
          className="fill-card stroke-border"
          strokeWidth={1.5}
        />
        <line
          x1={x1} y1={y1}
          x2={x2} y2={y2}
          className="stroke-foreground"
          strokeWidth={2}
          strokeLinecap="round"
          style={{ transition: "all 180ms linear" }}
        />
      </svg>
      <div className="flex items-center gap-2 text-xs text-muted-foreground">
        <Badge variant="outline" className="text-[10px]">
          Rotation: {ROTARY_ACTION_NAMES[settings.rotation_action] ?? "Unknown"}
        </Badge>
        <Badge variant="outline" className="text-[10px]">
          Button: {ROTARY_BUTTON_ACTION_NAMES[settings.button_action] ?? "Unknown"}
        </Badge>
        <Badge variant={isActive ? "default" : "secondary"} className="text-[10px] tabular-nums">
          Live: {Math.round(previewPercent)}%
        </Badge>
      </div>
    </div>
  );
}


function BindingEditor({
  label,
  binding,
  connected,
  onChange,
}: {
  label: string;
  binding: RotaryBinding;
  connected: boolean;
  onChange: (b: RotaryBinding) => void;
}) {
  return (
    <div className="flex flex-col divide-y">
      <FormRow label={`${label} Mode`}>
        <Select
          value={String(binding.mode)}
          disabled={!connected}
          items={selectItems(ROTARY_BINDING_MODES)}
          onValueChange={(v) => onChange({ ...binding, mode: Number(v) })}
        >
          <SelectTrigger className="w-36"><SelectValue /></SelectTrigger>
          <SelectContent>
            <SelectGroup>
              {Object.entries(ROTARY_BINDING_MODES).map(([name, val]) => (
                <SelectItem key={val} value={String(val)}>{name}</SelectItem>
              ))}
            </SelectGroup>
          </SelectContent>
        </Select>
      </FormRow>
      {binding.mode === ROTARY_BINDING_MODES.Keycode && (
        <>
          <FormRow label="Keycode (HID)" description="Raw HID keycode value">
            <input
              type="number"
              min={0}
              max={65535}
              value={binding.keycode}
              disabled={!connected}
              className="w-24 h-8 rounded-md border border-input bg-background px-2 text-sm"
              onChange={(e) => onChange({ ...binding, keycode: Number(e.target.value) & 0xffff })}
            />
          </FormRow>
          <FormRow label="Modifier Mask" description="Exact modifier byte (0-255)">
            <input
              type="number"
              min={0}
              max={255}
              value={binding.modifier_mask_exact}
              disabled={!connected}
              className="w-24 h-8 rounded-md border border-input bg-background px-2 text-sm"
              onChange={(e) => onChange({ ...binding, modifier_mask_exact: Number(e.target.value) & 0xff })}
            />
          </FormRow>
          <FormRow label="Fallback Keycode" description="Sent when no modifiers are held">
            <input
              type="number"
              min={0}
              max={65535}
              value={binding.fallback_no_mod_keycode}
              disabled={!connected}
              className="w-24 h-8 rounded-md border border-input bg-background px-2 text-sm"
              onChange={(e) => onChange({ ...binding, fallback_no_mod_keycode: Number(e.target.value) & 0xffff })}
            />
          </FormRow>
          <FormRow label="Layer Mode">
            <Select
              value={String(binding.layer_mode)}
              disabled={!connected}
              items={selectItems(ROTARY_BINDING_LAYER_MODES)}
              onValueChange={(v) => onChange({ ...binding, layer_mode: Number(v) })}
            >
              <SelectTrigger className="w-36"><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  {Object.entries(ROTARY_BINDING_LAYER_MODES).map(([name, val]) => (
                    <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </FormRow>
          {binding.layer_mode === ROTARY_BINDING_LAYER_MODES.Fixed && (
            <FormRow label="Layer Index">
              <Select
                value={String(binding.layer_index)}
                disabled={!connected}
                items={Array.from({ length: LAYER_COUNT }, (_, i) => ({ value: String(i), label: String(i) }))}
                onValueChange={(v) => onChange({ ...binding, layer_index: Number(v) })}
              >
                <SelectTrigger className="w-24"><SelectValue /></SelectTrigger>
                <SelectContent>
                  <SelectGroup>
                    {Array.from({ length: LAYER_COUNT }, (_, i) => (
                      <SelectItem key={i} value={String(i)}>{i}</SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
            </FormRow>
          )}
        </>
      )}
    </div>
  );
}

export default function Rotary() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const visible = usePageVisible();
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const rotaryQ = useQuery({
    queryKey: queryKeys.rotary.settings(),
    queryFn: () => kbheDevice.getRotaryEncoderSettings(),
    enabled: connected,
    refetchInterval: connected && visible ? ROTARY_SETTINGS_POLL_MS : false,
  });

  type RotaryPatch = Parameters<typeof kbheDevice.setRotaryEncoderSettings>[0];

  const mutation = useOptimisticMutation<RotaryEncoderSettings | null, RotaryPatch, boolean>({
    queryKey: queryKeys.rotary.settings(),
    mutationFn: async (full) => {
      markSaving();
      return kbheDevice.setRotaryEncoderSettings(full);
    },
    optimisticUpdate: (_cur, full) => full as RotaryEncoderSettings,
    onSuccess: () => {
      markSaved();
      void DeviceSessionManager.syncVolumeService();
    },
    onError: markError,
  });

  const write = (patch: Partial<RotaryPatch>) => {
    if (!rotaryQ.data) return;
    mutation.mutate({ ...rotaryQ.data, ...patch });
  };

  const liveRotary = useThrottledCall(async (patch: Partial<RotaryPatch>) => {
    if (!rotaryQ.data) return;
    await kbheDevice.setRotaryEncoderSettings({ ...rotaryQ.data, ...patch });
  });

  const livePreviewQ = useQuery({
    queryKey: [
      "rotary",
      "live-preview",
      rotaryQ.data?.rotation_action ?? -1,
      rotaryQ.data?.rgb_behavior ?? -1,
    ],
    queryFn: async () => {
      if (!rotaryQ.data) return null;
      return getRotaryLivePercent(rotaryQ.data);
    },
    enabled: connected && visible && !!rotaryQ.data,
    refetchInterval: connected && visible && !!rotaryQ.data ? ROTARY_PREVIEW_POLL_MS : false,
  });

  const previousLiveRef = useRef<number | null>(null);
  const [lastActivityAt, setLastActivityAt] = useState(0);

  useEffect(() => {
    const value = livePreviewQ.data ?? null;
    if (value == null) {
      previousLiveRef.current = value;
      return;
    }

    const previous = previousLiveRef.current;
    previousLiveRef.current = value;
    if (previous == null || Math.abs(previous - value) >= 0.2) {
      setLastActivityAt(Date.now());
    }
  }, [livePreviewQ.data]);

  const settingsSignature = useMemo(() => {
    if (!rotaryQ.data) return "";
    const s = rotaryQ.data;
    return `${s.rotation_action}:${s.button_action}:${s.rgb_behavior}:${s.progress_style}:${s.progress_effect_mode}`;
  }, [rotaryQ.data]);

  const previousSettingsSignatureRef = useRef("");
  useEffect(() => {
    if (!settingsSignature) return;
    if (previousSettingsSignatureRef.current && previousSettingsSignatureRef.current !== settingsSignature) {
      setLastActivityAt(Date.now());
    }
    previousSettingsSignatureRef.current = settingsSignature;
  }, [settingsSignature]);

  const isPreviewActive = Date.now() - lastActivityAt < 500;

  const s = rotaryQ.data;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <PageContent>
        <div className="flex justify-end">
          <AutosaveStatus state={saveState} />
        </div>

          {rotaryQ.isLoading ? (
            <SectionCard>
              <div className="space-y-3">{[0,1,2,3].map(i=><Skeleton key={i} className="h-9 w-full"/>)}</div>
            </SectionCard>
          ) : !s ? (
            <SectionCard>
              <p className="text-sm text-muted-foreground">Connect device to configure rotary encoder.</p>
            </SectionCard>
          ) : (
            <>
              <SectionCard>
                <RotaryVisual
                  settings={s}
                  livePercent={livePreviewQ.data ?? null}
                  isActive={isPreviewActive}
                />
              </SectionCard>

              <SectionCard title="Rotation">
                <div className="flex flex-col divide-y">
                  <FormRow label="Rotation Action" description="What rotating the knob controls">
                    <Select value={String(s.rotation_action)} disabled={!connected}
                      items={selectItems(ROTARY_ACTIONS)}
                      onValueChange={v => write({ rotation_action: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(ROTARY_ACTIONS).map(([name, val]) => (
                            <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                  {s.rotation_action === 0 && (
                    <FormRow
                      label="Volume Overlay"
                      description="Forwards system volume to the keyboard LED bar in real time"
                    >
                      <Badge variant={isVolumeServiceRunning() ? "default" : "secondary"}>
                        {isVolumeServiceRunning() ? "Active" : "Inactive"}
                      </Badge>
                    </FormRow>
                  )}
                  <FormRow label="Sensitivity">
                    <div className="w-44">
                      <CommitSlider min={1} max={10} step={1} value={s.sensitivity}
                        onLiveChange={(v) => liveRotary({ sensitivity: v })}
                        onCommit={(v) => write({ sensitivity: v })}
                        disabled={!connected} className="flex-1" />
                    </div>
                  </FormRow>
                  <FormRow label="Step Size">
                    <div className="w-44">
                      <CommitSlider min={1} max={20} step={1} value={s.step_size}
                        onLiveChange={(v) => liveRotary({ step_size: v })}
                        onCommit={(v) => write({ step_size: v })}
                        disabled={!connected} className="flex-1" />
                    </div>
                  </FormRow>
                  <FormRow label="Invert Direction">
                    <Switch checked={s.invert_direction} disabled={!connected}
                      onCheckedChange={v => write({ invert_direction: v })} />
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Button">
                <FormRow label="Button Action" description="What pressing the knob does">
                  <Select value={String(s.button_action)} disabled={!connected}
                    items={selectItems(ROTARY_BUTTON_ACTIONS)}
                    onValueChange={v => write({ button_action: Number(v) })}>
                    <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                    <SelectContent>
                      <SelectGroup>
                        {Object.entries(ROTARY_BUTTON_ACTIONS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectGroup>
                    </SelectContent>
                  </Select>
                </FormRow>
              </SectionCard>

              <SectionCard title="RGB Customizer" description="When rotation action is RGB Customizer">
                <div className="flex flex-col divide-y">
                  <FormRow label="RGB Behavior">
                    <Select value={String(s.rgb_behavior)} disabled={!connected}
                      items={selectItems(ROTARY_RGB_BEHAVIORS)}
                      onValueChange={v => write({ rgb_behavior: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(ROTARY_RGB_BEHAVIORS).map(([name, val]) => (
                            <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="RGB Effect Mode">
                    <Select value={String(s.rgb_effect_mode)} disabled={!connected}
                      items={selectItemsReverse(LED_EFFECT_NAMES)}
                      onValueChange={v => write({ rgb_effect_mode: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                            <SelectItem key={val} value={val}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                </div>
              </SectionCard>

              <SectionCard title="Key Bindings" description="Custom keycode bindings for CW, CCW and click">
                <div className="flex flex-col gap-4">
                  <div>
                    <p className="text-xs font-medium text-muted-foreground mb-2 uppercase tracking-wide">CW Binding</p>
                    <BindingEditor
                      label="CW"
                      binding={s.cw_binding}
                      connected={connected}
                      onChange={(b) => write({ cw_binding: b })}
                    />
                  </div>
                  <div>
                    <p className="text-xs font-medium text-muted-foreground mb-2 uppercase tracking-wide">CCW Binding</p>
                    <BindingEditor
                      label="CCW"
                      binding={s.ccw_binding}
                      connected={connected}
                      onChange={(b) => write({ ccw_binding: b })}
                    />
                  </div>
                  <div>
                    <p className="text-xs font-medium text-muted-foreground mb-2 uppercase tracking-wide">Click Binding</p>
                    <BindingEditor
                      label="Click"
                      binding={s.click_binding}
                      connected={connected}
                      onChange={(b) => write({ click_binding: b })}
                    />
                  </div>
                </div>
              </SectionCard>

              <SectionCard title="Progress Bar" description="LED progress bar display">
                <div className="flex flex-col divide-y">
                  <FormRow label="Style">
                    <Select value={String(s.progress_style)} disabled={!connected}
                      items={selectItems(ROTARY_PROGRESS_STYLES)}
                      onValueChange={v => write({ progress_style: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(ROTARY_PROGRESS_STYLES).map(([name, val]) => (
                            <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="Effect Mode">
                    <Select value={String(s.progress_effect_mode)} disabled={!connected}
                      items={selectItemsReverse(LED_EFFECT_NAMES)}
                      onValueChange={v => write({ progress_effect_mode: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                            <SelectItem key={val} value={val}>{name}</SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="Progress Color" description="Solid color for progress bar">
                    <ColorPicker
                      color={{
                        r: s.progress_color?.[0] ?? 255,
                        g: s.progress_color?.[1] ?? 255,
                        b: s.progress_color?.[2] ?? 255,
                      }}
                      onLiveChange={(c) => liveRotary({ progress_color: [c.r, c.g, c.b] })}
                      onChange={(c) => write({ progress_color: [c.r, c.g, c.b] })}
                    />
                  </FormRow>
                </div>
              </SectionCard>
            </>
          )}

      </PageContent>
    </div>
  );
}
