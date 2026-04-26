import { useState, useRef, useEffect, useMemo } from "react";
import { useQuery } from "@tanstack/react-query";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import type { KeySettings, AdcCaptureStatus as CaptureStatusT } from "@/lib/kbhe/device";
import { KEY_COUNT, SOCD_RESOLUTION_NAMES } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { previewKeys } from "@/constants/defaultLayout";
import BaseKeyboard from "@/components/baseKeyboard";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { SectionCard, FormRow } from "@/components/shared/SectionCard";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Slider } from "@/components/ui/slider";
import { Skeleton } from "@/components/ui/skeleton";
import { Label } from "@/components/ui/label";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { Sparkline } from "@/components/ui/sparkline";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { sliderVal, cn } from "@/lib/utils";
import {
  IconKeyboard,
  IconChartLine,
  IconBug,
  IconRefresh,
  IconPlayerPlay,
  IconWaveSquare,
  IconLock,
  IconArrowsExchange,
} from "@tabler/icons-react";

// ── Constants ────────────────────────────────────────────────────────────────

const KEY_LABELS = Array.from({ length: KEY_COUNT }, (_, i) =>
  previewKeys[i]?.baseLabel ?? String(i),
);

const GRID_COLS = 10;
const MAX_TRAVEL_MM = 4.0;

const GRAPH_COLORS = [
  "#3b82f6", "#ef4444", "#22c55e", "#f59e0b", "#8b5cf6",
  "#ec4899", "#14b8a6", "#f97316", "#6366f1", "#84cc16",
  "#06b6d4", "#d946ef", "#facc15", "#2dd4bf", "#fb923c",
];

const DATA_TYPES = ["raw", "filtered", "calibrated", "distance", "normalized"] as const;
type DataType = (typeof DATA_TYPES)[number];

const DATA_TYPE_LABELS: Record<DataType, string> = {
  raw: "Raw",
  filtered: "Filtered",
  calibrated: "Calibrated",
  distance: "Distance (0.01 mm)",
  normalized: "Normalized (0–255)",
};

const Y_DEFAULTS: Record<DataType, [number, number]> = {
  raw: [0, 4095],
  filtered: [0, 4095],
  calibrated: [0, 4095],
  distance: [0, 400],
  normalized: [0, 255],
};

const CHART_W = 800;
const CHART_H = 280;
const MCU_TREND_POINTS = 40;

function pushTrend(history: number[], value: number, maxPoints = MCU_TREND_POINTS): number[] {
  const next = [...history, value];
  if (next.length > maxPoints) {
    next.splice(0, next.length - maxPoints);
  }
  return next;
}

// ── Utilities ────────────────────────────────────────────────────────────────

function heatmapColor(t: number): string {
  const c = Math.max(0, Math.min(1, t));
  if (c < 0.25) return `rgb(0,${Math.round((c / 0.25) * 200)},255)`;
  if (c < 0.5) {
    const p = (c - 0.25) / 0.25;
    return `rgb(0,${200 + Math.round(p * 55)},${Math.round(255 * (1 - p))})`;
  }
  if (c < 0.75) {
    const p = (c - 0.5) / 0.25;
    return `rgb(${Math.round(255 * p)},255,0)`;
  }
  const p = (c - 0.75) / 0.25;
  return `rgb(255,${Math.round(255 * (1 - p))},0)`;
}

function DisconnectedBanner() {
  return (
    <div className="flex items-center justify-center py-12 text-muted-foreground">
      <p className="text-sm">Connect a device to use diagnostics.</p>
    </div>
  );
}

// ── Travel Tab ───────────────────────────────────────────────────────────────

function TravelTab({ connected, active }: { connected: boolean; active: boolean }) {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const [settings, setSettings] = useState<KeySettings[] | null>(null);
  const [loadingSettings, setLoadingSettings] = useState(false);

  const keyStatesQ = useQuery({
    queryKey: queryKeys.diagnostics.keyStates(),
    queryFn: () => kbheDevice.getKeyStates(),
    enabled: connected && active,
    refetchInterval: connected && active ? 100 : false,
  });

  const distances = keyStatesQ.data?.distances_mm ?? [];
  const states = keyStatesQ.data?.states ?? [];

  const selected = useMemo(() => {
    const s = new Set<number>();
    for (const k of selectedKeys) {
      if (k.startsWith("key-")) s.add(parseInt(k.replace("key-", ""), 10));
    }
    return s;
  }, [selectedKeys]);

  const loadThresholds = async () => {
    setLoadingSettings(true);
    try {
      setSettings(await kbheDevice.getAllKeySettings());
    } finally {
      setLoadingSettings(false);
    }
  };

  if (!connected) return <DisconnectedBanner />;

  const selectedArr = Array.from(selected).sort((a, b) => a - b);

  const keyColorMap: Record<string, string> = {};
  for (let i = 0; i < KEY_COUNT; i++) {
    const dist = distances[i] ?? 0;
    const t = Math.min(dist / MAX_TRAVEL_MM, 1);
    keyColorMap[`key-${i}`] = heatmapColor(t);
  }

  const renderKeyOverlay = (keyId: string) => {
    if (!keyId.startsWith("key-")) return undefined;
    const idx = parseInt(keyId.replace("key-", ""), 10);
    const dist = distances[idx] ?? 0;
    return (
      <span className="text-[8px] font-mono tabular-nums" style={{ color: "#fff", textShadow: "0 0 3px rgba(0,0,0,0.8)" }}>
        {dist.toFixed(1)}
      </span>
    );
  };

  return (
    <div className="flex flex-col gap-4">
      <SectionCard
        title="Key Travel Heatmap"
        description="Click keys to select · live travel distance overlay"
        headerRight={
          <div className="flex items-center gap-2">
            {keyStatesQ.data && (
              <Badge variant="outline" className="font-mono text-[10px]">
                {selectedArr.length} selected
              </Badge>
            )}
            <Button
              size="sm"
              variant="outline"
              className="h-7 gap-1.5"
              disabled={!connected || loadingSettings}
              onClick={() => void loadThresholds()}
            >
              <IconRefresh className={cn("size-3", loadingSettings && "animate-spin")} />
              Reload Thresholds
            </Button>
          </div>
        }
      >
        <BaseKeyboard
          mode="multi"
          onButtonClick={() => {}}
          showLayerSelector={false}
          showRotary={false}
          keyColorMap={keyColorMap}
          renderKeyOverlay={renderKeyOverlay}
        />
      </SectionCard>

      {selectedArr.length > 0 && (
        <SectionCard
          title="Selected Key Detail"
          description="Vertical travel bars with threshold markers"
        >
          <div className="flex gap-3 overflow-x-auto pb-2">
            {selectedArr.map((idx) => {
              const dist = distances[idx] ?? 0;
              const state = states[idx] ?? 0;
              const ks = settings?.find((s) => s.key_index === idx);
              const barH = 200;
              const toPx = (mm: number) =>
                Math.max(0, Math.min(barH, (mm / MAX_TRAVEL_MM) * barH));

              return (
                <div key={idx} className="flex flex-col items-center gap-1 shrink-0 w-10">
                  <span className="text-[10px] font-medium truncate w-full text-center">
                    {KEY_LABELS[idx]}
                  </span>
                  <Badge variant={state ? "default" : "outline"} className="text-[9px] h-4">
                    {state ? "Act" : "Idle"}
                  </Badge>
                  <div
                    className="relative border rounded bg-muted/30"
                    style={{ width: 36, height: barH }}
                  >
                    <div
                      className="absolute top-0 left-0 right-0 bg-primary/30 transition-all duration-75 rounded-t"
                      style={{ height: `${(dist / MAX_TRAVEL_MM) * 100}%` }}
                    />
                    <div
                      className="absolute left-0 right-0 h-0.5 bg-primary z-10"
                      style={{ top: toPx(dist) }}
                    />
                    {ks && (
                      <>
                        <div
                          className="absolute left-0 right-0 h-px bg-green-500 z-10"
                          style={{ top: toPx(ks.actuation_point_mm) }}
                          title={`Actuation: ${ks.actuation_point_mm} mm`}
                        />
                        <div
                          className="absolute left-0 right-0 h-px bg-red-500 z-10"
                          style={{ top: toPx(ks.release_point_mm) }}
                          title={`Release: ${ks.release_point_mm} mm`}
                        />
                        {ks.rapid_trigger_enabled && (
                          <>
                            <div
                              className="absolute left-0 right-0 bg-orange-400/30"
                              style={{
                                top: toPx(dist),
                                height: toPx(dist + ks.rapid_trigger_press) - toPx(dist),
                              }}
                            />
                            <div
                              className="absolute left-0 right-0 bg-yellow-400/30"
                              style={{
                                top: toPx(Math.max(0, dist - ks.rapid_trigger_release)),
                                height:
                                  toPx(dist) -
                                  toPx(Math.max(0, dist - ks.rapid_trigger_release)),
                              }}
                            />
                          </>
                        )}
                      </>
                    )}
                  </div>
                  <span className="text-[10px] tabular-nums font-mono">
                    {dist.toFixed(2)}
                  </span>
                  {ks && (
                    <div className="flex flex-col items-center text-[9px] text-muted-foreground leading-tight">
                      <span className="text-green-600">A {ks.actuation_point_mm}</span>
                      <span className="text-red-600">R {ks.release_point_mm}</span>
                      {ks.rapid_trigger_enabled && (
                        <span className="text-orange-500">
                          RT {ks.rapid_trigger_press}/{ks.rapid_trigger_release}
                        </span>
                      )}
                    </div>
                  )}
                </div>
              );
            })}
          </div>

          {!settings && (
            <p className="text-xs text-muted-foreground mt-2">
              Press &quot;Reload Thresholds&quot; to show actuation / release / RT markers.
            </p>
          )}
          {settings && (
            <div className="flex gap-4 mt-3 text-[10px] text-muted-foreground">
              <span className="flex items-center gap-1">
                <span className="inline-block w-3 h-0.5 bg-green-500" /> Actuation
              </span>
              <span className="flex items-center gap-1">
                <span className="inline-block w-3 h-0.5 bg-red-500" /> Release
              </span>
              <span className="flex items-center gap-1">
                <span className="inline-block w-3 h-1.5 bg-orange-400/50 rounded" /> RT Press
              </span>
              <span className="flex items-center gap-1">
                <span className="inline-block w-3 h-1.5 bg-yellow-400/50 rounded" /> RT Release
              </span>
            </div>
          )}
        </SectionCard>
      )}
    </div>
  );
}

// ── Raw ADC Tab ──────────────────────────────────────────────────────────────

function RawAdcTab({ connected, active }: { connected: boolean; active: boolean }) {
  const rawQ = useQuery({
    queryKey: ["diagnostics", "rawAdc"],
    queryFn: () => kbheDevice.getAllRawAdcValues(),
    enabled: connected && active,
    refetchInterval: connected && active ? 80 : false,
  });

  const values = useMemo(() => rawQ.data ?? [], [rawQ.data]);
  const stats = useMemo(() => {
    if (values.length === 0) return null;
    const min = Math.min(...values);
    const max = Math.max(...values);
    const avg = Math.round(values.reduce((a, b) => a + b, 0) / values.length);
    return { min, max, avg };
  }, [values]);

  if (!connected) return <DisconnectedBanner />;

  return (
    <SectionCard
      title="Raw ADC Values"
      description="All 82 keys · polled ~12 Hz"
      headerRight={
        <div className="flex items-center gap-3">
          {stats && (
            <div className="flex gap-2 text-[10px] font-mono">
              <span className="text-muted-foreground">Min <span className="text-foreground">{stats.min}</span></span>
              <span className="text-muted-foreground">Max <span className="text-foreground">{stats.max}</span></span>
              <span className="text-muted-foreground">Avg <span className="text-foreground">{stats.avg}</span></span>
            </div>
          )}
          <Button
            size="sm"
            variant="outline"
            className="h-7 gap-1.5"
            disabled={!connected || rawQ.isFetching}
            onClick={() => void rawQ.refetch()}
          >
            <IconRefresh className={cn("size-3", rawQ.isFetching && "animate-spin")} />
            Refresh
          </Button>
        </div>
      }
    >
      {values.length === 0 ? (
        <div
          className="grid gap-1"
          style={{ gridTemplateColumns: `repeat(${GRID_COLS}, minmax(0, 1fr))` }}
        >
          {Array.from({ length: KEY_COUNT }, (_, i) => (
            <Skeleton key={i} className="h-16 rounded-md" />
          ))}
        </div>
      ) : (
        <div
          className="grid gap-1"
          style={{ gridTemplateColumns: `repeat(${GRID_COLS}, minmax(0, 1fr))` }}
        >
          {values.map((v, i) => {
            const pct = Math.min(100, (v / 4095) * 100);
            return (
              <div
                key={i}
                className="flex flex-col items-center rounded-md border p-1 gap-0.5"
              >
                <span className="text-[9px] text-muted-foreground font-medium truncate w-full text-center">{KEY_LABELS[i]}</span>
                <div
                  className="w-3 bg-muted rounded-sm overflow-hidden relative"
                  style={{ height: 40 }}
                >
                  <div
                    className="absolute bottom-0 left-0 right-0 bg-primary transition-all duration-75 rounded-sm"
                    style={{ height: `${pct}%` }}
                  />
                </div>
                <span className="text-[9px] tabular-nums font-mono">{v}</span>
              </div>
            );
          })}
        </div>
      )}
    </SectionCard>
  );
}

// ── Graph Tab ────────────────────────────────────────────────────────────────

function TimeChart({
  buffers,
  depth,
  yMin,
  yMax,
  colors,
}: {
  buffers: Record<number, number[]>;
  depth: number;
  yMin: number;
  yMax: number;
  colors: Record<number, string>;
}) {
  const range = yMax - yMin || 1;
  const gridLines = 5;

  return (
    <svg
      viewBox={`0 0 ${CHART_W} ${CHART_H}`}
      className="w-full border rounded bg-muted/20"
      preserveAspectRatio="none"
      style={{ height: CHART_H }}
    >
      {Array.from({ length: gridLines + 1 }, (_, i) => {
        const y = (i / gridLines) * CHART_H;
        const val = yMax - (i / gridLines) * range;
        return (
          <g key={i}>
            <line
              x1={0}
              y1={y}
              x2={CHART_W}
              y2={y}
              stroke="currentColor"
              strokeOpacity={0.08}
            />
            <text
              x={4}
              y={y - 2}
              fontSize={9}
              fill="currentColor"
              opacity={0.4}
              fontFamily="monospace"
            >
              {val.toFixed(0)}
            </text>
          </g>
        );
      })}
      {Object.entries(buffers).map(([keyIdx, values]) => {
        if (values.length < 2) return null;
        const pts = values
          .map((v, i) => {
            const x = (i / Math.max(1, depth - 1)) * CHART_W;
            const y = CHART_H - ((v - yMin) / range) * CHART_H;
            return `${x.toFixed(1)},${Math.max(0, Math.min(CHART_H, y)).toFixed(1)}`;
          })
          .join(" ");
        return (
          <polyline
            key={keyIdx}
            points={pts}
            fill="none"
            stroke={colors[+keyIdx] ?? "#888"}
            strokeWidth={1.5}
            strokeLinejoin="round"
          />
        );
      })}
    </svg>
  );
}

function GraphTab({ connected, active }: { connected: boolean; active: boolean }) {
  const [dtype, setDtype] = useState<DataType>("filtered");
  const [graphKeys, setGraphKeys] = useState<number[]>([]);
  const [depth, setDepth] = useState(200);
  const [yMin, setYMin] = useState(0);
  const [yMax, setYMax] = useState(4095);
  const [buffers, setBuffers] = useState<Record<number, number[]>>({});
  const lastUpdate = useRef(0);

  useEffect(() => {
    const [lo, hi] = Y_DEFAULTS[dtype];
    setYMin(lo);
    setYMax(hi);
  }, [dtype]);

  const dataQ = useQuery({
    queryKey: ["diagnostics", "graphData", dtype],
    queryFn: async (): Promise<number[] | null> => {
      switch (dtype) {
        case "raw":
          return kbheDevice.getAllRawAdcValues();
        case "filtered":
          return kbheDevice.getAllFilteredAdcValues();
        case "calibrated":
          return kbheDevice.getAllCalibratedAdcValues();
        case "distance": {
          const s = await kbheDevice.getKeyStates();
          return s?.distances_01mm ?? null;
        }
        case "normalized": {
          const s = await kbheDevice.getKeyStates();
          return s?.distances.map(Number) ?? null;
        }
      }
    },
    enabled: connected && active && graphKeys.length > 0,
    refetchInterval: connected && active && graphKeys.length > 0 ? 50 : false,
  });

  useEffect(() => {
    const data = dataQ.data;
    if (!data || dataQ.dataUpdatedAt === lastUpdate.current) return;
    lastUpdate.current = dataQ.dataUpdatedAt;
    setBuffers((prev) => {
      const next: Record<number, number[]> = {};
      for (const ki of graphKeys) {
        const arr = [...(prev[ki] ?? []), data[ki] ?? 0];
        if (arr.length > depth) arr.splice(0, arr.length - depth);
        next[ki] = arr;
      }
      return next;
    });
  }, [dataQ.data, dataQ.dataUpdatedAt, graphKeys, depth]);

  const toggleGraphKey = (idx: number) => {
    setGraphKeys((prev) =>
      prev.includes(idx) ? prev.filter((k) => k !== idx) : [...prev, idx],
    );
  };

  const colors = useMemo(
    () =>
      Object.fromEntries(
        graphKeys.map((k, i) => [k, GRAPH_COLORS[i % GRAPH_COLORS.length]]),
      ),
    [graphKeys],
  );

  const heatValues = dataQ.data ?? [];

  if (!connected) return <DisconnectedBanner />;

  return (
    <div className="flex flex-col gap-4">
      <SectionCard title="Time Series Graph" description="Multi-key rolling history">
        <div className="flex flex-wrap items-center gap-3 mb-3">
          <div className="flex items-center gap-2">
            <Label className="text-xs shrink-0">Type</Label>
            <Select
              value={dtype}
              items={DATA_TYPES.map((t) => ({ value: t, label: DATA_TYPE_LABELS[t] }))}
              onValueChange={(v) => {
                setDtype(v as DataType);
                setBuffers({});
              }}
            >
              <SelectTrigger size="sm" className="w-40 text-xs">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  {DATA_TYPES.map((t) => (
                    <SelectItem key={t} value={t}>{DATA_TYPE_LABELS[t]}</SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </div>
          <div className="flex items-center gap-2">
            <Label className="text-xs shrink-0">Depth</Label>
            <div className="w-28">
              <Slider
                value={[depth]}
                min={50}
                max={2000}
                step={50}
                onValueChange={(v) => setDepth(sliderVal(v) ?? 200)}
              />
            </div>
            <span className="text-[10px] tabular-nums font-mono w-10">{depth}</span>
          </div>
          <div className="flex items-center gap-1.5">
            <Label className="text-xs shrink-0">Y</Label>
            <Input
              type="number"
              value={yMin}
              onChange={(e) => setYMin(+e.target.value)}
              className="w-16 h-7 text-xs"
            />
            <span className="text-xs text-muted-foreground">–</span>
            <Input
              type="number"
              value={yMax}
              onChange={(e) => setYMax(+e.target.value)}
              className="w-16 h-7 text-xs"
            />
          </div>
          <Button
            size="sm"
            variant="outline"
            className="h-7 text-xs"
            onClick={() => {
              const allVals = Object.values(buffers).flat();
              if (allVals.length === 0) return;
              const lo = Math.min(...allVals);
              const hi = Math.max(...allVals);
              const margin = (hi - lo) * 0.1 || 10;
              setYMin(Math.floor(lo - margin));
              setYMax(Math.ceil(hi + margin));
            }}
          >
            Auto Y
          </Button>
          <Button
            size="sm"
            variant="outline"
            className="h-7 text-xs"
            onClick={() => setBuffers({})}
          >
            Clear
          </Button>
        </div>

        {graphKeys.length === 0 ? (
          <p className="text-sm text-muted-foreground py-4">
            Select keys below to start graphing.
          </p>
        ) : (
          <TimeChart
            buffers={buffers}
            depth={depth}
            yMin={yMin}
            yMax={yMax}
            colors={colors}
          />
        )}

        {graphKeys.length > 0 && (
          <div className="flex items-start justify-between gap-4 mt-2">
            <div className="flex flex-wrap gap-2">
              {graphKeys.map((k, i) => (
                <Badge key={k} variant="outline" className="gap-1 text-[10px]">
                  <span
                    className="inline-block w-2 h-2 rounded-full"
                    style={{
                      backgroundColor: GRAPH_COLORS[i % GRAPH_COLORS.length],
                    }}
                  />
                  {KEY_LABELS[k]} (#{k})
                </Badge>
              ))}
            </div>
            {graphKeys.length > 0 && buffers[graphKeys[0]]?.length > 0 && (() => {
              const vals = buffers[graphKeys[0]];
              return (
                <div className="flex gap-3 text-[10px] font-mono shrink-0 border rounded px-2 py-1">
                  <span className="text-muted-foreground">Cur <span className="text-foreground">{vals[vals.length - 1]?.toFixed?.(1) ?? vals[vals.length - 1]}</span></span>
                  <span className="text-muted-foreground">Min <span className="text-foreground">{Math.min(...vals).toFixed?.(1)}</span></span>
                  <span className="text-muted-foreground">Max <span className="text-foreground">{Math.max(...vals).toFixed?.(1)}</span></span>
                  <span className="text-muted-foreground">Avg <span className="text-foreground">{(vals.reduce((a, b) => a + b, 0) / vals.length).toFixed(1)}</span></span>
                </div>
              );
            })()}
          </div>
        )}
      </SectionCard>

      <SectionCard
        title="Key Selection"
        description="Click keys to add / remove from graph · heatmap shows current values"
      >
        <div
          className="grid gap-1"
          style={{ gridTemplateColumns: `repeat(${GRID_COLS}, minmax(0, 1fr))` }}
        >
          {Array.from({ length: KEY_COUNT }, (_, i) => {
            const inGraph = graphKeys.includes(i);
            const hv = heatValues[i] ?? 0;
            const [lo, hi] = Y_DEFAULTS[dtype];
            const t = hi > lo ? (hv - lo) / (hi - lo) : 0;
            return (
              <button
                key={i}
                type="button"
                onClick={() => toggleGraphKey(i)}
                className={cn(
                  "rounded-md p-1 text-[9px] transition-all cursor-pointer min-h-8 flex flex-col items-center justify-center",
                  inGraph ? "ring-2 ring-primary ring-offset-1" : "",
                )}
                style={{
                  backgroundColor:
                    heatValues.length > 0 ? heatmapColor(Math.max(0, Math.min(1, t))) : undefined,
                  color: heatValues.length > 0 && t > 0.45 ? "#000" : undefined,
                }}
              >
                <span className="font-medium truncate w-full text-center">
                  {KEY_LABELS[i]}
                </span>
              </button>
            );
          })}
        </div>
      </SectionCard>
    </div>
  );
}

// ── Debug Tab ────────────────────────────────────────────────────────────────

function DebugTab({ connected, active }: { connected: boolean; active: boolean }) {
  const { firmwareVersion } = useDeviceSession();

  // Live monitor rates
  const adcQ = useQuery({
    queryKey: queryKeys.diagnostics.adcValues(),
    queryFn: () => kbheDevice.getAdcValues(),
    enabled: connected && active,
    refetchInterval: connected && active ? 250 : false,
  });

  // MCU metrics
  const mcuQ = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected && active,
    refetchInterval: connected && active ? 500 : false,
  });

  const [mcuTrends, setMcuTrends] = useState({
    temperature: [] as number[],
    vref: [] as number[],
    coreClock: [] as number[],
    scanRate: [] as number[],
    loadPercent: [] as number[],
    scanCycle: [] as number[],
    work: [] as number[],
    loadPermille: [] as number[],
  });

  useEffect(() => {
    const metrics = mcuQ.data;
    if (!metrics) {
      return;
    }

    setMcuTrends((prev) => ({
      temperature:
        metrics.temperature_valid && metrics.temperature_c != null
          ? pushTrend(prev.temperature, metrics.temperature_c)
          : prev.temperature,
      vref: pushTrend(prev.vref, metrics.vref_mv),
      coreClock: pushTrend(prev.coreClock, metrics.core_clock_hz),
      scanRate: pushTrend(prev.scanRate, metrics.scan_rate_hz),
      loadPercent: pushTrend(prev.loadPercent, metrics.load_percent),
      scanCycle: pushTrend(prev.scanCycle, metrics.scan_cycle_us),
      work: pushTrend(prev.work, metrics.work_us),
      loadPermille: pushTrend(prev.loadPermille, metrics.load_permille),
    }));
  }, [mcuQ.data]);

  // Lock states
  const lockQ = useQuery({
    queryKey: queryKeys.device.lockStates(),
    queryFn: () => kbheDevice.getLockStates(),
    enabled: connected && active,
    refetchInterval: connected && active ? 1000 : false,
  });

  // Config snapshot (one-time)
  const optionsQ = useQuery({
    queryKey: queryKeys.device.options(),
    queryFn: () => kbheDevice.getOptions(),
    enabled: connected && active,
  });
  const nkroQ = useQuery({
    queryKey: queryKeys.device.nkroEnabled(),
    queryFn: () => kbheDevice.getNkroEnabled(),
    enabled: connected && active,
  });
  const gamepadQ = useQuery({
    queryKey: queryKeys.gamepad.settings(),
    queryFn: () => kbheDevice.getGamepadSettings(),
    enabled: connected && active,
  });

  // Filter controls
  const filterEnabledQ = useQuery({
    queryKey: queryKeys.device.filterEnabled(),
    queryFn: () => kbheDevice.getFilterEnabled(),
    enabled: connected && active,
  });
  const filterParamsQ = useQuery({
    queryKey: queryKeys.device.filterParams(),
    queryFn: () => kbheDevice.getFilterParams(),
    enabled: connected && active,
  });

  const [filterEnabled, setFilterEnabledLocal] = useState<boolean | null>(null);
  const [noise, setNoise] = useState(30);
  const [alphaMin, setAlphaMin] = useState(32);
  const [alphaMax, setAlphaMax] = useState(4);

  useEffect(() => {
    if (filterEnabledQ.data != null) setFilterEnabledLocal(filterEnabledQ.data);
  }, [filterEnabledQ.data]);

  useEffect(() => {
    const p = filterParamsQ.data;
    if (!p) return;
    setNoise(p.noise_band);
    setAlphaMin(p.alpha_min_denom);
    setAlphaMax(p.alpha_max_denom);
  }, [filterParamsQ.data]);

  const saveFilter = async () => {
    if (filterEnabled != null) await kbheDevice.setFilterEnabled(filterEnabled);
    await kbheDevice.setFilterParams(noise, alphaMin, alphaMax);
    void filterEnabledQ.refetch();
    void filterParamsQ.refetch();
  };

  // ADC Capture
  const [captureKey, setCaptureKey] = useState(0);
  const [captureDuration, setCaptureDuration] = useState(500);
  const [captureStatus, setCaptureStatus] = useState<CaptureStatusT | null>(null);
  const [captureData, setCaptureData] = useState<{
    raw: number[];
    filtered: number[];
  } | null>(null);
  const [capturing, setCapturing] = useState(false);

  const startCapture = async () => {
    setCapturing(true);
    setCaptureData(null);
    const st = await kbheDevice.adcCaptureStart(captureKey, captureDuration);
    setCaptureStatus(st);
    if (!st?.active) {
      setCapturing(false);
      return;
    }

    const poll = async () => {
      const s = await kbheDevice.adcCaptureStatus();
      setCaptureStatus(s);
      if (s?.active) {
        setTimeout(() => void poll(), 100);
        return;
      }
      const raw: number[] = [];
      const filtered: number[] = [];
      const total = s?.sample_count ?? 0;
      let idx = 0;
      while (idx < total) {
        const chunk = await kbheDevice.adcCaptureRead(idx, 12);
        if (!chunk || chunk.sample_count === 0) break;
        raw.push(...chunk.raw_samples);
        filtered.push(...chunk.filtered_samples);
        idx += chunk.sample_count;
      }
      setCaptureData({ raw, filtered });
      setCapturing(false);
    };
    setTimeout(() => void poll(), 100);
  };

  // SOCD
  const [allSettings, setAllSettings] = useState<KeySettings[] | null>(null);
  const [loadingSettings, setLoadingSettings] = useState(false);

  const loadSettings = async () => {
    setLoadingSettings(true);
    try {
      setAllSettings(await kbheDevice.getAllKeySettings());
    } finally {
      setLoadingSettings(false);
    }
  };

  const socdPairs = useMemo(() => {
    if (!allSettings) return [];
    const seen = new Set<string>();
    const pairs: { a: number; b: number; resolution: number }[] = [];
    for (const ks of allSettings) {
      if (ks.socd_pair == null) continue;
      const key = `${Math.min(ks.key_index, ks.socd_pair)}-${Math.max(ks.key_index, ks.socd_pair)}`;
      if (seen.has(key)) continue;
      seen.add(key);
      pairs.push({ a: ks.key_index, b: ks.socd_pair, resolution: ks.socd_resolution });
    }
    return pairs;
  }, [allSettings]);

  if (!connected) return <DisconnectedBanner />;

  const adc = adcQ.data;
  const mcu = mcuQ.data;
  const locks = lockQ.data;
  const opts = optionsQ.data;

  return (
    <div className="flex flex-col gap-4">
      {/* Live Monitor Rates */}
      <SectionCard title="Live Monitor" description="Scan and task timing from ADC endpoint">
        {!adc ? (
          <Skeleton className="h-16" />
        ) : (
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 text-xs">
            <MetricTile label="Scan Rate" value={`${adc.scan_rate_hz}`} unit="Hz" />
            <MetricTile label="Scan Time" value={`${adc.scan_time_us}`} unit="µs" />
            <MetricTile label="Payload" badge={adc.adc_payload_format} />
            <MetricTile
              label="Format"
              badge={adc.task_times_us ? "Extended" : "Legacy"}
            />
          </div>
        )}
        {adc?.task_times_us && (
          <div className="mt-3 grid grid-cols-2 sm:grid-cols-4 gap-2 text-[11px]">
            {Object.entries(adc.task_times_us).map(([k, v]) => (
              <div key={k} className="flex justify-between border rounded px-2 py-1">
                <span className="text-muted-foreground font-mono">{k}</span>
                <span className="tabular-nums font-mono">{v} µs</span>
              </div>
            ))}
          </div>
        )}
        {adc?.analog_monitor_us && (
          <div className="mt-2 grid grid-cols-2 sm:grid-cols-4 gap-2 text-[11px]">
            {Object.entries(adc.analog_monitor_us).map(([k, v]) => (
              <div key={k} className="flex justify-between border rounded px-2 py-1">
                <span className="text-muted-foreground font-mono">{k}</span>
                <span className="tabular-nums font-mono">{v}</span>
              </div>
            ))}
          </div>
        )}
      </SectionCard>

      {/* MCU Metrics */}
      <SectionCard
        title="MCU Metrics"
        description="Temperature, voltage, clock, load"
        headerRight={
          <Button
            size="sm"
            variant="outline"
            className="h-7 gap-1.5"
            onClick={() => void mcuQ.refetch()}
          >
            <IconRefresh className="size-3" />
          </Button>
        }
      >
        {!mcu ? (
          <Skeleton className="h-20" />
        ) : (
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 text-xs">
            <MetricTile
              label="Temperature"
              value={
                mcu.temperature_valid && mcu.temperature_c != null
                  ? mcu.temperature_c.toFixed(1)
                  : "—"
              }
              unit={mcu.temperature_valid ? "deg C" : ""}
              trendValues={mcuTrends.temperature}
            />
            <MetricTile
              label="Vref"
              value={`${mcu.vref_mv}`}
              unit="mV"
              trendValues={mcuTrends.vref}
            />
            <MetricTile
              label="Core Clock"
              value={`${(mcu.core_clock_hz / 1e6).toFixed(0)}`}
              unit="MHz"
              trendValues={mcuTrends.coreClock}
            />
            <MetricTile
              label="Scan Rate"
              value={`${mcu.scan_rate_hz}`}
              unit="Hz"
              trendValues={mcuTrends.scanRate}
            />
            <MetricTile
              label="CPU Load"
              value={mcu.load_percent.toFixed(1)}
              unit="%"
              trendValues={mcuTrends.loadPercent}
            />
            <MetricTile
              label="Scan Cycle"
              value={`${mcu.scan_cycle_us}`}
              unit="us"
              trendValues={mcuTrends.scanCycle}
            />
            <MetricTile
              label="Work"
              value={`${mcu.work_us}`}
              unit="us"
              trendValues={mcuTrends.work}
            />
            <MetricTile
              label="Load (permille)"
              value={`${mcu.load_permille}`}
              trendValues={mcuTrends.loadPermille}
            />
          </div>
        )}
      </SectionCard>

      {/* Lock States */}
      <SectionCard title="Lock States">
        {!locks ? (
          <Skeleton className="h-8" />
        ) : (
          <div className="flex gap-3">
            {(
              [
                ["Caps Lock", locks.caps_lock],
                ["Num Lock", locks.num_lock],
                ["Scroll Lock", locks.scroll_lock],
              ] as const
            ).map(([label, on]) => (
              <Badge key={label} variant={on ? "default" : "outline"} className="gap-1.5">
                <IconLock className="size-3" />
                {label}
              </Badge>
            ))}
          </div>
        )}
      </SectionCard>

      {/* Config Snapshot */}
      <SectionCard
        title="Config Snapshot"
        description="Firmware version, options, NKRO, gamepad"
      >
        <div className="grid grid-cols-2 sm:grid-cols-3 gap-x-6 gap-y-2 text-xs">
          <ConfigRow label="Firmware" value={firmwareVersion ?? "—"} />
          <ConfigRow label="Keyboard" on={opts?.keyboard_enabled} />
          <ConfigRow label="Gamepad" on={opts?.gamepad_enabled} />
          <ConfigRow
            label="NKRO"
            on={nkroQ.data == null ? undefined : nkroQ.data}
          />
          <ConfigRow label="Raw HID Echo" on={opts?.raw_hid_echo} />
          <ConfigRow
            label="LED Thermal Protection"
            on={opts?.led_thermal_protection_enabled}
          />
          {gamepadQ.data && (
            <ConfigRow
              label="Gamepad API"
              value={gamepadQ.data.api_mode === 0 ? "DirectInput" : "XInput"}
            />
          )}
        </div>
      </SectionCard>

      {/* ADC Filter Controls */}
      <SectionCard
        title="ADC Filter"
        description="Digital noise filter parameters"
        headerRight={
          <Button
            size="sm"
            variant="outline"
            className="h-7"
            onClick={() => void saveFilter()}
          >
            Save
          </Button>
        }
      >
        <div className="flex flex-col gap-3">
          <FormRow label="Filter Enabled" description="Toggle digital noise filter on / off">
            <Switch
              checked={filterEnabled ?? false}
              onCheckedChange={(v) => setFilterEnabledLocal(v)}
              disabled={filterEnabledQ.isLoading}
            />
          </FormRow>
          <FormRow label="Noise Band" description="1–255 · larger = more smoothing">
            <Input
              type="number"
              min={1}
              max={255}
              value={noise}
              onChange={(e) => setNoise(Math.max(1, Math.min(255, +e.target.value)))}
              className="w-20 h-7 text-xs"
            />
          </FormRow>
          <FormRow label="Alpha Min Denominator" description="Min smoothing factor denominator">
            <Input
              type="number"
              min={1}
              max={255}
              value={alphaMin}
              onChange={(e) => setAlphaMin(Math.max(1, Math.min(255, +e.target.value)))}
              className="w-20 h-7 text-xs"
            />
          </FormRow>
          <FormRow
            label="Alpha Max Denominator"
            description="Max smoothing factor denominator"
          >
            <Input
              type="number"
              min={1}
              max={255}
              value={alphaMax}
              onChange={(e) => setAlphaMax(Math.max(1, Math.min(255, +e.target.value)))}
              className="w-20 h-7 text-xs"
            />
          </FormRow>
        </div>
      </SectionCard>

      {/* ADC Capture */}
      <SectionCard
        title="ADC Capture"
        description="Capture raw + filtered waveform for a single key"
      >
        <div className="flex items-center gap-3 flex-wrap mb-3">
          <div className="flex items-center gap-1.5">
            <Label className="text-xs">Key</Label>
            <Input
              type="number"
              min={0}
              max={KEY_COUNT - 1}
              value={captureKey}
              onChange={(e) =>
                setCaptureKey(Math.max(0, Math.min(KEY_COUNT - 1, +e.target.value)))
              }
              className="w-16 h-7 text-xs"
            />
          </div>
          <div className="flex items-center gap-1.5">
            <Label className="text-xs">Duration (ms)</Label>
            <Input
              type="number"
              min={1}
              max={10000}
              value={captureDuration}
              onChange={(e) => setCaptureDuration(+e.target.value)}
              className="w-20 h-7 text-xs"
            />
          </div>
          <Button
            size="sm"
            className="h-7 gap-1.5"
            disabled={capturing}
            onClick={() => void startCapture()}
          >
            <IconPlayerPlay className="size-3" /> Start Capture
          </Button>
        </div>

        {captureStatus && (
          <div className="text-xs space-y-1 mb-3">
            <div className="flex gap-4 flex-wrap">
              <span>
                Active:{" "}
                <Badge
                  variant={captureStatus.active ? "default" : "outline"}
                  className="text-[10px]"
                >
                  {captureStatus.active ? "Yes" : "No"}
                </Badge>
              </span>
              <span>Key: {captureStatus.key_index}</span>
              <span>Samples: {captureStatus.sample_count}</span>
              <span>Overflow: {captureStatus.overflow_count}</span>
            </div>
            {captureStatus.active && <Progress value={50} className="h-1.5" />}
          </div>
        )}

        {captureData && captureData.raw.length > 0 && (
          <div className="border rounded p-2">
            <div className="flex gap-4 mb-1 text-[10px] text-muted-foreground">
              <span className="flex items-center gap-1">
                <span className="inline-block w-3 h-0.5 bg-blue-500" /> Raw (
                {captureData.raw.length})
              </span>
              <span className="flex items-center gap-1">
                <span className="inline-block w-3 h-0.5 bg-green-500" /> Filtered (
                {captureData.filtered.length})
              </span>
            </div>
            <svg
              viewBox={`0 0 ${captureData.raw.length} 4095`}
              className="w-full h-32 bg-muted/20 rounded"
              preserveAspectRatio="none"
            >
              {[
                { data: captureData.raw, color: "#3b82f6" },
                { data: captureData.filtered, color: "#22c55e" },
              ].map(({ data: arr, color }) => (
                <polyline
                  key={color}
                  points={arr.map((v, i) => `${i},${4095 - v}`).join(" ")}
                  fill="none"
                  stroke={color}
                  strokeWidth={Math.max(1, captureData.raw.length / 200)}
                  strokeLinejoin="round"
                />
              ))}
            </svg>
          </div>
        )}
      </SectionCard>

      {/* SOCD Visualization */}
      <SectionCard
        title="SOCD Pairs"
        description="Configured simultaneous opposing cardinal direction pairs"
        headerRight={
          <Button
            size="sm"
            variant="outline"
            className="h-7 gap-1.5"
            disabled={loadingSettings}
            onClick={() => void loadSettings()}
          >
            <IconRefresh className={cn("size-3", loadingSettings && "animate-spin")} />
            Load Settings
          </Button>
        }
      >
        {!allSettings ? (
          <p className="text-sm text-muted-foreground">
            Press &quot;Load Settings&quot; to view SOCD pairs.
          </p>
        ) : socdPairs.length === 0 ? (
          <p className="text-sm text-muted-foreground">No SOCD pairs configured.</p>
        ) : (
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
            {socdPairs.map(({ a, b, resolution }) => (
              <div
                key={`${a}-${b}`}
                className="flex items-center gap-2 border rounded-md px-3 py-2"
              >
                <Badge variant="outline" className="font-mono text-[10px]">
                  {KEY_LABELS[a]} (#{a})
                </Badge>
                <IconArrowsExchange className="size-4 text-muted-foreground shrink-0" />
                <Badge variant="outline" className="font-mono text-[10px]">
                  {KEY_LABELS[b]} (#{b})
                </Badge>
                <Badge variant="secondary" className="ml-auto text-[10px]">
                  {SOCD_RESOLUTION_NAMES[resolution] ?? `Mode ${resolution}`}
                </Badge>
              </div>
            ))}
          </div>
        )}
      </SectionCard>
    </div>
  );
}

// ── Shared presentational helpers ────────────────────────────────────────────

function MetricTile({
  label,
  value,
  unit,
  badge,
  trendValues,
}: {
  label: string;
  value?: string;
  unit?: string;
  badge?: string;
  trendValues?: number[];
}) {
  return (
    <div className="rounded-lg border p-2">
      <p className="text-muted-foreground text-[11px]">{label}</p>
      {badge ? (
        <Badge variant="outline" className="text-[10px] mt-0.5">
          {badge}
        </Badge>
      ) : (
        <div className="mt-0.5 flex items-center justify-between gap-2">
          <p className="text-lg font-semibold tabular-nums">
            {value}{" "}
            {unit && <span className="text-xs font-normal text-muted-foreground">{unit}</span>}
          </p>
          <Sparkline values={trendValues ?? []} className="w-16" />
        </div>
      )}
    </div>
  );
}

function ConfigRow({
  label,
  value,
  on,
}: {
  label: string;
  value?: string;
  on?: boolean;
}) {
  return (
    <div className="flex justify-between">
      <span className="text-muted-foreground">{label}</span>
      {value !== undefined ? (
        <span className="tabular-nums font-mono">{value}</span>
      ) : (
        <Badge
          variant={on ? "default" : "outline"}
          className="text-[10px]"
        >
          {on == null ? "—" : on ? "On" : "Off"}
        </Badge>
      )}
    </div>
  );
}

// ── Page ─────────────────────────────────────────────────────────────────────

export default function Diagnostics() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const [activeTab, setActiveTab] = useState("travel");

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs value={activeTab} onValueChange={setActiveTab} className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4 py-1">
            <div className="mx-auto flex max-w-5xl items-center justify-between gap-4">
              <TabsList className="h-9">
                <TabsTrigger value="travel" className="gap-1.5">
                  <IconKeyboard className="size-3" /> Travel
                </TabsTrigger>
                <TabsTrigger value="raw" className="gap-1.5">
                  <IconWaveSquare className="size-3" /> Raw ADC
                </TabsTrigger>
                <TabsTrigger value="graph" className="gap-1.5">
                  <IconChartLine className="size-3" /> Graph
                </TabsTrigger>
                <TabsTrigger value="debug" className="gap-1.5">
                  <IconBug className="size-3" /> Debug
                </TabsTrigger>
              </TabsList>
              <Badge variant="destructive" className="text-[10px] h-5">
                DEV
              </Badge>
            </div>
          </div>

          <TabsContent value="travel" className="flex-1 min-h-0 mt-0">
            <ScrollArea className="h-full">
              <div className="p-4">
                <div className="flex flex-col gap-4 max-w-4xl mx-auto">
                  <TravelTab connected={connected} active={activeTab === "travel"} />
                </div>
              </div>
            </ScrollArea>
          </TabsContent>

          <TabsContent value="raw" className="flex-1 min-h-0 mt-0">
            <ScrollArea className="h-full">
              <div className="p-4">
                <div className="flex flex-col gap-4 max-w-4xl mx-auto">
                  <RawAdcTab connected={connected} active={activeTab === "raw"} />
                </div>
              </div>
            </ScrollArea>
          </TabsContent>

          <TabsContent value="graph" className="flex-1 min-h-0 mt-0">
            <ScrollArea className="h-full">
              <div className="p-4">
                <div className="flex flex-col gap-4 max-w-5xl mx-auto">
                  <GraphTab connected={connected} active={activeTab === "graph"} />
                </div>
              </div>
            </ScrollArea>
          </TabsContent>

          <TabsContent value="debug" className="flex-1 min-h-0 mt-0">
            <ScrollArea className="h-full">
              <div className="p-4">
                <div className="flex flex-col gap-4 max-w-3xl mx-auto">
                  <DebugTab connected={connected} active={activeTab === "debug"} />
                </div>
              </div>
            </ScrollArea>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
