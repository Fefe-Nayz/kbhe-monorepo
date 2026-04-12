import { useState, useRef, useCallback, useMemo } from "react";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { selectItemsReverse } from "@/lib/utils";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { LEDEffect, KEY_COUNT } from "@/lib/kbhe/protocol";
import BaseKeyboard from "@/components/baseKeyboard";

import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { ColorPicker, type RGBColor } from "@/components/color-picker";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Switch } from "@/components/ui/switch";
import { CommitSlider } from "@/components/ui/commit-slider";
import { RadioGroup, RadioGroupItem } from "@/components/ui/radio-group";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import { Button } from "@/components/ui/button";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  IconBrush,
  IconTrash,
  IconRainbow,
  IconUpload,
  IconDownload,
  IconFileImport,
  IconFileExport,
  IconDropletFilled,
} from "@tabler/icons-react";

// ---------------------------------------------------------------------------
// Effect metadata — ported from Python kbhe_tool
// ---------------------------------------------------------------------------

interface EffectGroupEntry {
  id: number;
  name: string;
}

const EFFECT_GROUPS: { category: string; effects: EffectGroupEntry[] }[] = [
  {
    category: "Software Control",
    effects: [
      { id: 0, name: "Matrix (Software)" },
      { id: 4, name: "Solid Color" },
      { id: 14, name: "Third-Party Live" },
    ],
  },
  {
    category: "Ambient Motion",
    effects: [
      { id: 1, name: "Rainbow Wave" },
      { id: 2, name: "Breathing" },
      { id: 3, name: "Static Rainbow" },
      { id: 5, name: "Plasma" },
      { id: 6, name: "Fire" },
      { id: 7, name: "Ocean Waves" },
      { id: 8, name: "Matrix Rain" },
      { id: 9, name: "Sparkle" },
      { id: 10, name: "Breathing Rainbow" },
      { id: 11, name: "Spiral" },
      { id: 12, name: "Color Cycle" },
    ],
  },
  {
    category: "Reactive",
    effects: [
      { id: 13, name: "Reactive (Key Press)" },
      { id: 15, name: "Sensor Distance" },
    ],
  },
];

const EFFECT_DESCRIPTIONS: Record<number, string> = {
  0: "Uses the editable matrix pattern from the Matrix tab.",
  1: "Animated rainbow sweep across the physical layout.",
  2: "Pulses the selected color.",
  3: "Rainbow colors without motion.",
  4: "A single steady color fill.",
  5: "Fluid plasma motion.",
  6: "Animated fire simulation.",
  7: "Layered wave motion.",
  8: "Digital rain effect.",
  9: "Random sparkles.",
  10: "Breathing plus hue drift.",
  11: "Spiral motion pattern.",
  12: "Continuous cycling through hues.",
  13: "Expanding color ripples from each key press.",
  14: "Shows the live frame controlled externally.",
  15: "Each key color follows its travel distance.",
};

type ParamSpec =
  | { kind: "slider"; index: number; label: string; default: number; min?: number; max?: number }
  | { kind: "toggle"; index: number; label: string; default: number }
  | { kind: "select"; index: number; label: string; default: number; options: [number, string][] }
  | { kind: "color"; label: string };

const EFFECT_PARAM_METADATA: Record<number, ParamSpec[]> = {
  1: [
    { kind: "slider", index: 0, label: "Horizontal Scale", default: 160 },
    { kind: "slider", index: 1, label: "Vertical Scale", default: 96 },
    { kind: "slider", index: 2, label: "Drift", default: 160 },
    { kind: "slider", index: 3, label: "Saturation", default: 255 },
  ],
  2: [
    { kind: "slider", index: 0, label: "Brightness Floor", default: 24 },
    { kind: "slider", index: 1, label: "Brightness Ceiling", default: 255 },
    { kind: "slider", index: 2, label: "Plateau", default: 48 },
  ],
  3: [
    { kind: "slider", index: 0, label: "Horizontal Scale", default: 160 },
    { kind: "slider", index: 1, label: "Vertical Scale", default: 120 },
    { kind: "slider", index: 2, label: "Saturation", default: 144 },
    { kind: "slider", index: 3, label: "Brightness", default: 255 },
  ],
  4: [
    { kind: "slider", index: 0, label: "Effect Brightness", default: 255 },
  ],
  5: [
    { kind: "slider", index: 0, label: "Motion Depth", default: 96 },
    { kind: "slider", index: 1, label: "Saturation", default: 192 },
    { kind: "slider", index: 2, label: "Radial Warp", default: 128 },
    { kind: "slider", index: 3, label: "Brightness", default: 255 },
  ],
  6: [
    { kind: "slider", index: 0, label: "Heat Boost", default: 160 },
    { kind: "slider", index: 1, label: "Ember Floor", default: 96 },
    { kind: "slider", index: 2, label: "Cooling", default: 96 },
    { kind: "select", index: 3, label: "Palette", default: 0, options: [[0, "Classic"], [1, "Magma"], [2, "Electric Blue"]] },
  ],
  7: [
    { kind: "slider", index: 0, label: "Hue Bias", default: 160 },
    { kind: "slider", index: 1, label: "Depth Dimming", default: 64 },
    { kind: "toggle", index: 2, label: "Foam Highlight", default: 1 },
    { kind: "slider", index: 3, label: "Crest Speed", default: 160 },
  ],
  8: [
    { kind: "slider", index: 0, label: "Trail Length", default: 64 },
    { kind: "slider", index: 1, label: "Head Size", default: 160 },
    { kind: "slider", index: 2, label: "Density", default: 96 },
    { kind: "toggle", index: 3, label: "White Heads", default: 1 },
    { kind: "slider", index: 4, label: "Hue Bias", default: 0 },
  ],
  9: [
    { kind: "slider", index: 0, label: "Density", default: 48 },
    { kind: "slider", index: 1, label: "Sparkle Brightness", default: 224 },
    { kind: "slider", index: 2, label: "Rainbow Mix", default: 160 },
    { kind: "slider", index: 3, label: "Ambient Glow", default: 0 },
  ],
  10: [
    { kind: "slider", index: 0, label: "Brightness Floor", default: 24 },
    { kind: "slider", index: 1, label: "Hue Drift", default: 192 },
    { kind: "slider", index: 2, label: "Saturation", default: 255 },
  ],
  11: [
    { kind: "slider", index: 0, label: "Twist", default: 160 },
    { kind: "slider", index: 1, label: "Radial Scale", default: 96 },
    { kind: "slider", index: 2, label: "Orbit Speed", default: 128 },
    { kind: "slider", index: 3, label: "Saturation", default: 255 },
  ],
  12: [
    { kind: "slider", index: 0, label: "Hue Step", default: 64 },
    { kind: "slider", index: 1, label: "Saturation", default: 255 },
    { kind: "slider", index: 2, label: "Brightness", default: 255 },
    { kind: "slider", index: 3, label: "Color Mix", default: 0 },
  ],
  13: [
    { kind: "color", label: "Reactive Color" },
    { kind: "slider", index: 0, label: "Decay", default: 72 },
    { kind: "slider", index: 1, label: "Spread", default: 128 },
    { kind: "slider", index: 2, label: "Base Glow", default: 0, max: 64 },
    { kind: "toggle", index: 3, label: "White Core", default: 1 },
    { kind: "slider", index: 4, label: "Gain", default: 224 },
  ],
  15: [
    { kind: "slider", index: 0, label: "Brightness Floor", default: 32 },
    { kind: "slider", index: 1, label: "Hue Span", default: 170, min: 1 },
    { kind: "slider", index: 2, label: "Saturation", default: 255 },
    { kind: "toggle", index: 3, label: "Reverse Gradient", default: 0 },
  ],
};

const DIAGNOSTIC_MODES: Record<number, string> = {
  0: "Normal",
  1: "DMA Stress",
  2: "CPU Stress",
};

const QUICK_COLORS: RGBColor[] = [
  { r: 255, g: 0, b: 0 },
  { r: 255, g: 128, b: 0 },
  { r: 255, g: 255, b: 0 },
  { r: 0, g: 255, b: 0 },
  { r: 0, g: 255, b: 255 },
  { r: 0, g: 0, b: 255 },
  { r: 128, g: 0, b: 255 },
  { r: 255, g: 255, b: 255 },
];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------


function rgbToHex(r: number, g: number, b: number): string {
  return `#${[r, g, b].map((v) => v.toString(16).padStart(2, "0")).join("")}`;
}

function pixelsToRgbArray(pixels: number[]): RGBColor[] {
  const out: RGBColor[] = [];
  for (let i = 0; i < KEY_COUNT; i++) {
    out.push({
      r: pixels[i * 3] ?? 0,
      g: pixels[i * 3 + 1] ?? 0,
      b: pixels[i * 3 + 2] ?? 0,
    });
  }
  return out;
}

function rgbArrayToPixels(colors: RGBColor[]): number[] {
  const out: number[] = [];
  for (let i = 0; i < KEY_COUNT; i++) {
    const c = colors[i] ?? { r: 0, g: 0, b: 0 };
    out.push(c.r, c.g, c.b);
  }
  return out;
}

function effectName(id: number): string {
  for (const g of EFFECT_GROUPS) {
    for (const e of g.effects) {
      if (e.id === id) return e.name;
    }
  }
  return `Mode ${id}`;
}

// ---------------------------------------------------------------------------
// Matrix Keyboard sub-component
// ---------------------------------------------------------------------------

function MatrixKeyboard({
  pixelColors,
  connected,
  onPaint,
}: {
  pixelColors: RGBColor[];
  connected: boolean;
  onPaint: (idx: number) => void;
}) {
  const keyColorMap = useMemo(() => {
    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const c = pixelColors[i];
      if (c) map[`key-${i}`] = rgbToHex(c.r, c.g, c.b);
    }
    return map;
  }, [pixelColors]);

  const handleClick = useCallback(
    (ids: string[] | string) => {
      if (!connected) return;
      const id = Array.isArray(ids) ? ids[0] : ids;
      if (!id?.startsWith("key-")) return;
      const idx = parseInt(id.replace("key-", ""), 10);
      onPaint(idx);
    },
    [connected, onPaint],
  );

  return (
    <BaseKeyboard
      mode="single"
      onButtonClick={handleClick}
      showLayerSelector={false}
      showRotary={false}
      keyColorMap={keyColorMap}
    />
  );
}

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

export default function Lighting() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();
  const [activeTab, setActiveTab] = useState("effects");
  const [paintColor, setPaintColor] = useState<RGBColor>({ r: 255, g: 0, b: 0 });
  const fileInputRef = useRef<HTMLInputElement>(null);

  // ---- Queries ----

  const enabledQ = useQuery({
    queryKey: queryKeys.led.enabled(),
    queryFn: () => kbheDevice.ledGetEnabled(),
    enabled: connected,
  });

  const brightnessQ = useQuery({
    queryKey: queryKeys.led.brightness(),
    queryFn: () => kbheDevice.ledGetBrightness(),
    enabled: connected,
  });

  const effectQ = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled: connected,
  });

  const speedQ = useQuery({
    queryKey: queryKeys.led.effectSpeed(),
    queryFn: () => kbheDevice.getLedEffectSpeed(),
    enabled: connected,
  });

  const colorQ = useQuery({
    queryKey: queryKeys.led.effectColor(),
    queryFn: () => kbheDevice.getLedEffectColor(),
    enabled: connected,
  });

  const fpsQ = useQuery({
    queryKey: queryKeys.led.fpsLimit(),
    queryFn: () => kbheDevice.getLedFpsLimit(),
    enabled: connected,
  });

  const diagnosticQ = useQuery({
    queryKey: queryKeys.led.diagnostic(),
    queryFn: () => kbheDevice.getLedDiagnostic(),
    enabled: connected,
  });

  const currentEffect = effectQ.data ?? LEDEffect.SOLID;

  const paramsQ = useQuery({
    queryKey: queryKeys.led.effectParams(currentEffect),
    queryFn: () => kbheDevice.getLedEffectParams(currentEffect),
    enabled: connected && effectQ.data != null,
  });

  const isMatrixMode = currentEffect === LEDEffect.MATRIX;
  const matrixVisible = activeTab === "matrix";

  const allPixelsQ = useQuery({
    queryKey: queryKeys.led.allPixels(),
    queryFn: () => kbheDevice.ledDownloadAll(),
    enabled: connected && matrixVisible,
    refetchInterval: matrixVisible && !isMatrixMode ? 2000 : false,
  });

  const pixelColors = useMemo(
    () => (allPixelsQ.data ? pixelsToRgbArray(allPixelsQ.data) : null),
    [allPixelsQ.data],
  );

  // ---- Mutations ----

  const enabledMut = useMutation({
    mutationFn: async (v: boolean) => { markSaving(); await kbheDevice.ledSetEnabled(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.enabled() }); },
    onError: markError,
  });

  const brightnessMut = useOptimisticMutation<number, number, void>({
    queryKey: queryKeys.led.brightness(),
    mutationFn: async (v) => { markSaving(); await kbheDevice.ledSetBrightness(v); },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: markSaved,
    onError: markError,
  });

  const effectMut = useMutation({
    mutationFn: async (v: number) => { markSaving(); await kbheDevice.setLedEffect(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.effect() }); },
    onError: markError,
  });

  const speedMut = useOptimisticMutation<number, number, void>({
    queryKey: queryKeys.led.effectSpeed(),
    mutationFn: async (v) => { markSaving(); await kbheDevice.setLedEffectSpeed(v); },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: markSaved,
    onError: markError,
  });

  const colorMut = useOptimisticMutation<number[], RGBColor, void>({
    queryKey: queryKeys.led.effectColor(),
    mutationFn: async (rgb) => { markSaving(); await kbheDevice.setLedEffectColor(rgb.r, rgb.g, rgb.b); },
    optimisticUpdate: (_cur, rgb) => [rgb.r, rgb.g, rgb.b],
    onSuccess: markSaved,
    onError: markError,
  });

  const fpsMut = useOptimisticMutation<number, number, void>({
    queryKey: queryKeys.led.fpsLimit(),
    mutationFn: async (v) => { markSaving(); await kbheDevice.setLedFpsLimit(v); },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: markSaved,
    onError: markError,
  });

  const paramsMut = useOptimisticMutation<number[], number[], void>({
    queryKey: queryKeys.led.effectParams(currentEffect),
    mutationFn: async (params) => { markSaving(); await kbheDevice.setLedEffectParams(currentEffect, params); },
    optimisticUpdate: (_cur, params) => params,
    onSuccess: markSaved,
    onError: markError,
  });

  const diagnosticMut = useMutation({
    mutationFn: async (v: number) => { markSaving(); await kbheDevice.setLedDiagnostic(v); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.diagnostic() }); },
    onError: markError,
  });

  const pixelMut = useMutation({
    mutationFn: async (args: { index: number; color: RGBColor }) => {
      await kbheDevice.ledSetPixel(args.index, args.color.r, args.color.g, args.color.b);
    },
    onSuccess: () => { void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() }); },
  });

  const fillMut = useMutation({
    mutationFn: async (c: RGBColor) => { markSaving(); await kbheDevice.ledFill(c.r, c.g, c.b); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() }); },
    onError: markError,
  });

  const clearMut = useMutation({
    mutationFn: async () => { markSaving(); await kbheDevice.ledClear(); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() }); },
    onError: markError,
  });

  const rainbowMut = useMutation({
    mutationFn: async () => { markSaving(); await kbheDevice.ledTestRainbow(); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() }); },
    onError: markError,
  });

  const uploadMut = useMutation({
    mutationFn: async (pixels: number[]) => { markSaving(); await kbheDevice.ledUploadAll(pixels); },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() }); },
    onError: markError,
  });

  const downloadMut = useMutation({
    mutationFn: async () => {
      markSaving();
      const data = await kbheDevice.ledDownloadAll();
      if (!data) throw new Error("Download failed");
      qc.setQueryData(queryKeys.led.allPixels(), data);
    },
    onSuccess: markSaved,
    onError: markError,
  });


  // ---- Live throttled calls (runtime-only, no flash — firmware auto-saves after 750ms) ----

  const liveBrightness = useThrottledCall(async (v: number) => {
    await kbheDevice.ledSetBrightness(v);
  });

  const liveSpeed = useThrottledCall(async (v: number) => {
    await kbheDevice.setLedEffectSpeed(v);
  });

  const liveFps = useThrottledCall(async (v: number) => {
    await kbheDevice.setLedFpsLimit(v);
  });

  const liveParams = useThrottledCall(async (params: number[]) => {
    await kbheDevice.setLedEffectParams(currentEffect, params);
  });

  const liveColor = useThrottledCall(async (c: RGBColor) => {
    await kbheDevice.setLedEffectColor(c.r, c.g, c.b);
  });

  // ---- File I/O ----

  const handleExport = useCallback(() => {
    const brightness = brightnessQ.data ?? 128;
    const pixels = allPixelsQ.data ?? new Array(KEY_COUNT * 3).fill(0);
    const buf = new Uint8Array(1 + KEY_COUNT * 3);
    buf[0] = brightness & 0xff;
    for (let i = 0; i < KEY_COUNT * 3; i++) {
      buf[1 + i] = (pixels[i] ?? 0) & 0xff;
    }
    const blob = new Blob([buf], { type: "application/octet-stream" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "keyboard.led";
    a.click();
    URL.revokeObjectURL(url);
  }, [brightnessQ.data, allPixelsQ.data]);

  const handleImport = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      const file = e.target.files?.[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        const buf = new Uint8Array(reader.result as ArrayBuffer);
        if (buf.length < 1 + KEY_COUNT * 3) return;
        const brightness = buf[0];
        const pixels = Array.from(buf.slice(1, 1 + KEY_COUNT * 3));
        brightnessMut.mutate(brightness);
        uploadMut.mutate(pixels);
      };
      reader.readAsArrayBuffer(file);
      e.target.value = "";
    },
    [brightnessMut, uploadMut],
  );

  // ---- Render helpers ----

  const effectColor: RGBColor = colorQ.data
    ? { r: colorQ.data[0], g: colorQ.data[1], b: colorQ.data[2] }
    : { r: 255, g: 255, b: 255 };

  const currentMeta = EFFECT_PARAM_METADATA[currentEffect];

  function renderEffectParams() {
    if (!currentMeta || !paramsQ.data) {
      if (effectQ.data != null && !EFFECT_PARAM_METADATA[currentEffect]) {
        return (
          <p className="text-sm text-muted-foreground py-2">
            This effect has no additional tuning controls.
          </p>
        );
      }
      return null;
    }

    const params = paramsQ.data;

    return (
      <div className="flex flex-col gap-3">
        {currentMeta.map((spec, i) => {
          if (spec.kind === "color") {
            return (
              <div key={i} className="flex items-center justify-between">
                <Label className="text-sm">{spec.label}</Label>
                <div className="flex items-center gap-2">
                  <ColorPicker color={effectColor} onLiveChange={(c) => liveColor(c)} onChange={(c) => colorMut.mutate(c)} />
                </div>
              </div>
            );
          }

          if (spec.kind === "toggle") {
            const checked = (params[spec.index] ?? spec.default) > 0;
            return (
              <div key={i} className="flex items-center justify-between">
                <Label className="text-sm">{spec.label}</Label>
                <Switch
                  checked={checked}
                  disabled={!connected}
                  onCheckedChange={(v) => {
                    const next = [...params];
                    next[spec.index] = v ? 1 : 0;
                    paramsMut.mutate(next);
                  }}
                />
              </div>
            );
          }

          if (spec.kind === "select") {
            const val = params[spec.index] ?? spec.default;
            const optMap: Record<string, string> = {};
            for (const [v, l] of spec.options) optMap[String(v)] = l;
            return (
              <div key={i} className="flex items-center justify-between">
                <Label className="text-sm">{spec.label}</Label>
                <Select
                  value={String(val)}
                  items={selectItemsReverse(optMap)}
                  onValueChange={(v) => {
                    const next = [...params];
                    next[spec.index] = Number(v);
                    paramsMut.mutate(next);
                  }}
                >
                  <SelectTrigger size="sm" className="w-36">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectGroup>
                      {spec.options.map(([v, l]) => (
                        <SelectItem key={v} value={String(v)}>{l}</SelectItem>
                      ))}
                    </SelectGroup>
                  </SelectContent>
                </Select>
              </div>
            );
          }

          // slider
          const val = params[spec.index] ?? spec.default;
          const sMin = spec.min ?? 0;
          const sMax = spec.max ?? 255;
          return (
            <div key={i} className="grid gap-1.5">
              <div className="flex items-center justify-between">
                <Label className="text-sm">{spec.label}</Label>
              </div>
              <CommitSlider
                min={sMin}
                max={sMax}
                step={1}
                value={val}
                onLiveChange={(v) => {
                  const next = [...params];
                  next[spec.index] = v;
                  liveParams(next);
                }}
                onCommit={(v) => {
                  const next = [...params];
                  next[spec.index] = v;
                  paramsMut.mutate(next);
                }}
                disabled={!connected}
              />
            </div>
          );
        })}
      </div>
    );
  }

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs value={activeTab} onValueChange={setActiveTab} className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4 py-1">
            <div className="mx-auto flex max-w-5xl items-center justify-between gap-4">
              <TabsList className="h-9">
                <TabsTrigger value="effects">Effects</TabsTrigger>
                <TabsTrigger value="matrix">Matrix</TabsTrigger>
              </TabsList>
              <AutosaveStatus state={saveState} />
            </div>
          </div>

          {/* === Effects Tab === */}
          <TabsContent value="effects" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="mx-auto grid max-w-5xl gap-4 lg:grid-cols-[1fr_320px]">
              {/* Left column */}
              <div className="flex flex-col gap-4">
                <SectionCard title="LED Control">
                  <div className="flex flex-col divide-y">
                    <FormRow label="LEDs Enabled">
                      <Switch
                        checked={enabledQ.data ?? false}
                        disabled={!connected}
                        onCheckedChange={(v) => enabledMut.mutate(v)}
                      />
                    </FormRow>
                    <FormRow label="Global Brightness">
                      <div className="flex items-center gap-3 w-44">
                        {brightnessQ.data == null ? (
                          <Skeleton className="h-5 w-full" />
                        ) : (
                          <CommitSlider
                            min={0} max={255} step={1}
                            value={brightnessQ.data}
                            onLiveChange={(v) => liveBrightness(v)}
                            onCommit={(v) => brightnessMut.mutate(v)}
                            disabled={!connected} className="flex-1"
                          />
                        )}
                      </div>
                    </FormRow>
                  </div>
                </SectionCard>

                <SectionCard
                  title="Effect Mode"
                  description={EFFECT_DESCRIPTIONS[currentEffect]}
                >
                  {effectQ.isLoading ? (
                    <div className="space-y-2">{Array.from({ length: 4 }, (_, i) => <Skeleton key={i} className="h-6 w-48" />)}</div>
                  ) : (
                    <RadioGroup
                      value={String(effectQ.data ?? 0)}
                      onValueChange={(v: string) => effectMut.mutate(Number(v))}
                      disabled={!connected}
                      className="flex flex-col gap-4"
                    >
                      {EFFECT_GROUPS.map((group) => (
                        <div key={group.category}>
                          <p className="text-xs font-medium text-muted-foreground mb-2 uppercase tracking-wide">{group.category}</p>
                          <div className="grid grid-cols-2 gap-x-6 gap-y-1.5">
                            {group.effects.map((eff) => (
                              <div key={eff.id} className="flex items-center gap-2">
                                <RadioGroupItem value={String(eff.id)} id={`effect-${eff.id}`} />
                                <Label htmlFor={`effect-${eff.id}`} className="text-sm font-normal cursor-pointer">{eff.name}</Label>
                              </div>
                            ))}
                          </div>
                        </div>
                      ))}
                    </RadioGroup>
                  )}
                </SectionCard>
              </div>

              {/* Right column */}
              <div className="flex flex-col gap-4">
                <SectionCard
                  title="Effect Tuning"
                  description={effectName(currentEffect)}
                >
                  {paramsQ.isLoading ? (
                    <div className="space-y-3">{Array.from({ length: 4 }, (_, i) => <Skeleton key={i} className="h-9 w-full" />)}</div>
                  ) : (
                    renderEffectParams()
                  )}
                </SectionCard>

                <SectionCard title="Effect Speed">
                  <div className="flex items-center gap-3">
                    {speedQ.data == null ? (
                      <Skeleton className="h-5 w-full" />
                    ) : (
                      <CommitSlider
                        min={1} max={255} step={1}
                        value={speedQ.data}
                        onLiveChange={(v) => liveSpeed(v)}
                        onCommit={(v) => speedMut.mutate(v)}
                        disabled={!connected} className="flex-1"
                      />
                    )}
                  </div>
                </SectionCard>

                <SectionCard title="Effect Color" description="Used by Solid, Breathing, Reactive, and related effects.">
                  {colorQ.data == null ? (
                    <Skeleton className="h-8 w-16" />
                  ) : (
                    <div className="flex flex-col gap-3">
                      <div className="flex items-center gap-3">
                        <ColorPicker color={effectColor} onLiveChange={(c) => liveColor(c)} onChange={(c) => colorMut.mutate(c)} />
                        <span className="text-xs font-mono text-muted-foreground">
                          {rgbToHex(effectColor.r, effectColor.g, effectColor.b)}
                        </span>
                      </div>
                      <div className="flex gap-1.5">
                        {QUICK_COLORS.map((c, i) => (
                          <button
                            key={i}
                            className="size-6 rounded-md border border-border transition-transform hover:scale-110 focus-visible:ring-2 focus-visible:ring-ring"
                            style={{ backgroundColor: rgbToHex(c.r, c.g, c.b) }}
                            title={rgbToHex(c.r, c.g, c.b)}
                            onClick={() => colorMut.mutate(c)}
                          />
                        ))}
                      </div>
                    </div>
                  )}
                </SectionCard>

                <SectionCard title="FPS Limit">
                  <div className="flex items-center gap-3">
                    {fpsQ.data == null ? (
                      <Skeleton className="h-5 w-full" />
                    ) : (
                      <CommitSlider
                        min={0} max={120} step={1}
                        value={fpsQ.data}
                        onLiveChange={(v) => liveFps(v)}
                        onCommit={(v) => fpsMut.mutate(v)}
                        disabled={!connected} className="flex-1"
                      />
                    )}
                  </div>
                </SectionCard>

                {/* <SectionCard title="LED Diagnostic Mode" description="Stress-test modes for hardware verification">
                  {diagnosticQ.isLoading ? (
                    <Skeleton className="h-8 w-36" />
                  ) : (
                    <RadioGroup
                      value={String(diagnosticQ.data ?? 0)}
                      onValueChange={(v: string) => diagnosticMut.mutate(Number(v))}
                      disabled={!connected}
                      className="flex gap-4"
                    >
                      {Object.entries(DIAGNOSTIC_MODES).map(([val, name]) => (
                        <div key={val} className="flex items-center gap-2">
                          <RadioGroupItem value={val} id={`diag-${val}`} />
                          <Label htmlFor={`diag-${val}`} className="text-sm font-normal cursor-pointer">{name}</Label>
                        </div>
                      ))}
                    </RadioGroup>
                  )}
                </SectionCard> */}
              </div>
            </div>
          </TabsContent>

          {/* === Matrix Tab === */}
          <TabsContent value="matrix" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-3xl mx-auto">
              <SectionCard title="Paint Color">
                <div className="flex items-center gap-4">
                  <ColorPicker color={paintColor} onChange={setPaintColor} />
                  <div className="flex items-center gap-1.5 text-sm text-muted-foreground">
                    <IconDropletFilled className="size-4" style={{ color: rgbToHex(paintColor.r, paintColor.g, paintColor.b) }} />
                    <span className="font-mono text-xs">{rgbToHex(paintColor.r, paintColor.g, paintColor.b)}</span>
                  </div>
                </div>
              </SectionCard>

              <SectionCard
                title="LED Matrix"
                description={isMatrixMode ? "Software matrix mode — click to paint" : "Live LED state (read-only while an effect is active)"}
                headerRight={
                  <div className="flex gap-1.5 flex-wrap justify-end">
                    <Button variant="outline" size="sm" disabled={!connected} onClick={() => fillMut.mutate(paintColor)}>
                      <IconBrush className="size-3.5 mr-1" />Fill
                    </Button>
                    <Button variant="outline" size="sm" disabled={!connected} onClick={() => clearMut.mutate()}>
                      <IconTrash className="size-3.5 mr-1" />Clear
                    </Button>
                    <Button variant="outline" size="sm" disabled={!connected} onClick={() => rainbowMut.mutate()}>
                      <IconRainbow className="size-3.5 mr-1" />Rainbow
                    </Button>
                  </div>
                }
              >
                {pixelColors ? (
                  <MatrixKeyboard
                    pixelColors={pixelColors}
                    connected={connected}
                    onPaint={(idx) => pixelMut.mutate({ index: idx, color: paintColor })}
                  />
                ) : allPixelsQ.isLoading ? (
                  <Skeleton className="h-40 w-full" />
                ) : (
                  <p className="text-sm text-muted-foreground py-4">
                    {connected ? "Failed to read LED data. Try refreshing." : "Connect a device to view the LED matrix."}
                  </p>
                )}
              </SectionCard>

              <SectionCard title="Transfer">
                <div className="flex flex-wrap gap-2">
                  <Button variant="outline" size="sm" disabled={!connected} onClick={() => { if (pixelColors) uploadMut.mutate(rgbArrayToPixels(pixelColors)); }}>
                    <IconUpload className="size-3.5 mr-1" />Upload All
                  </Button>
                  <Button variant="outline" size="sm" disabled={!connected} onClick={() => downloadMut.mutate()}>
                    <IconDownload className="size-3.5 mr-1" />Download All
                  </Button>
                  <div className="w-px bg-border mx-1 self-stretch" />
                  <Button variant="outline" size="sm" onClick={handleExport} disabled={!connected || !allPixelsQ.data}>
                    <IconFileExport className="size-3.5 mr-1" />Export .led
                  </Button>
                  <Button variant="outline" size="sm" onClick={() => fileInputRef.current?.click()} disabled={!connected}>
                    <IconFileImport className="size-3.5 mr-1" />Import .led
                  </Button>
                  <input ref={fileInputRef} type="file" accept=".led" className="hidden" aria-label="Import .led file" onChange={handleImport} />
                </div>
              </SectionCard>
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
