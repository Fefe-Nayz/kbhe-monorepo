import { useQuery } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useDeviceSession, DeviceSessionManager } from "@/lib/kbhe/session";
import { kbheDevice, type RotaryEncoderSettings } from "@/lib/kbhe/device";
import { isVolumeServiceRunning } from "@/lib/kbhe/volume-service";
import {
  ROTARY_ACTIONS, ROTARY_BUTTON_ACTIONS, ROTARY_RGB_BEHAVIORS, ROTARY_PROGRESS_STYLES,
  LED_EFFECT_NAMES,
} from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { PageHeader } from "@/components/shared/PageLayout";
import { CommitSlider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import {
  Select, SelectContent, SelectItem, SelectTrigger, SelectValue,
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

function RotaryVisual({ settings }: { settings: RotaryEncoderSettings }) {
  const progressColor = settings.progress_color
    ? `rgb(${settings.progress_color[0]}, ${settings.progress_color[1]}, ${settings.progress_color[2]})`
    : "rgb(40, 210, 64)";

  const r = 56;
  const cx = 80;
  const cy = 80;
  const size = 160;

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
          strokeDasharray={`${(2 * Math.PI * (r - 8)) * 0.7} ${(2 * Math.PI * (r - 8)) * 0.3}`}
          strokeLinecap="round"
          transform={`rotate(-126 ${cx} ${cy})`}
          opacity={0.6}
        />
        <circle
          cx={cx} cy={cy} r={20}
          className="fill-card stroke-border"
          strokeWidth={1.5}
        />
        <line
          x1={cx} y1={cy - 10}
          x2={cx} y2={cy - 18}
          className="stroke-foreground"
          strokeWidth={2}
          strokeLinecap="round"
        />
        <text x={cx - r - 4} y={cy} fontSize={10} className="fill-muted-foreground" textAnchor="end" dominantBaseline="middle">
          CCW
        </text>
        <text x={cx + r + 4} y={cy} fontSize={10} className="fill-muted-foreground" textAnchor="start" dominantBaseline="middle">
          CW
        </text>
      </svg>
      <div className="flex items-center gap-2 text-xs text-muted-foreground">
        <Badge variant="outline" className="text-[10px]">
          Rotation: {ROTARY_ACTION_NAMES[settings.rotation_action] ?? "Unknown"}
        </Badge>
        <Badge variant="outline" className="text-[10px]">
          Button: {ROTARY_BUTTON_ACTION_NAMES[settings.button_action] ?? "Unknown"}
        </Badge>
      </div>
    </div>
  );
}


export default function Rotary() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const { saveState, markSaving, markSaved, markError } = useAutosave();

  const rotaryQ = useQuery({
    queryKey: queryKeys.rotary.settings(),
    queryFn: () => kbheDevice.getRotaryEncoderSettings(),
    enabled: connected,
  });

  type RotaryPatch = Parameters<typeof kbheDevice.setRotaryEncoderSettings>[0];

  const mutation = useOptimisticMutation<RotaryEncoderSettings | null, RotaryPatch>({
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
  });

  const write = (patch: Partial<RotaryPatch>) => {
    if (!rotaryQ.data) return;
    mutation.mutate({ ...rotaryQ.data, ...patch });
  };

  const s = rotaryQ.data;

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <PageHeader title="Rotary Encoder" description="Rotation, button, RGB, and progress bar settings" />
        <AutosaveStatus state={saveState} />
      </div>

      <div className="flex-1 overflow-y-auto p-4">
        <div className="flex flex-col gap-4 max-w-3xl mx-auto">

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
                <RotaryVisual settings={s} />
              </SectionCard>

              <SectionCard title="Rotation">
                <div className="flex flex-col divide-y">
                  <FormRow label="Rotation Action" description="What rotating the knob controls">
                    <Select value={String(s.rotation_action)} disabled={!connected}
                      items={selectItems(ROTARY_ACTIONS)}
                      onValueChange={v => write({ rotation_action: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(ROTARY_ACTIONS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
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
                        onCommit={(v) => write({ sensitivity: v })}
                        disabled={!connected} className="flex-1" />
                    </div>
                  </FormRow>
                  <FormRow label="Step Size">
                    <div className="w-44">
                      <CommitSlider min={1} max={20} step={1} value={s.step_size}
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
                      {Object.entries(ROTARY_BUTTON_ACTIONS).map(([name, val]) => (
                        <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                      ))}
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
                        {Object.entries(ROTARY_RGB_BEHAVIORS).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="RGB Effect Mode">
                    <Select value={String(s.rgb_effect_mode)} disabled={!connected}
                      items={selectItemsReverse(LED_EFFECT_NAMES)}
                      onValueChange={v => write({ rgb_effect_mode: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                          <SelectItem key={val} value={val}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
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
                        {Object.entries(ROTARY_PROGRESS_STYLES).map(([name, val]) => (
                          <SelectItem key={val} value={String(val)}>{name}</SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </FormRow>
                  <FormRow label="Effect Mode">
                    <Select value={String(s.progress_effect_mode)} disabled={!connected}
                      items={selectItemsReverse(LED_EFFECT_NAMES)}
                      onValueChange={v => write({ progress_effect_mode: Number(v) })}>
                      <SelectTrigger className="w-44"><SelectValue /></SelectTrigger>
                      <SelectContent>
                        {Object.entries(LED_EFFECT_NAMES).map(([val, name]) => (
                          <SelectItem key={val} value={val}>{name}</SelectItem>
                        ))}
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
                      onChange={(c) => write({
                        progress_color: [c.r, c.g, c.b],
                      })}
                    />
                  </FormRow>
                </div>
              </SectionCard>
            </>
          )}

        </div>
      </div>
    </div>
  );
}
