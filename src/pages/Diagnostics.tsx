import { useState, useRef, useEffect, useCallback } from "react";
import { useQuery } from "@tanstack/react-query";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import type { AdcDebugValues } from "@/lib/kbhe/device";
import { queryKeys } from "@/lib/query/keys";
import { PageHeader } from "@/components/shared/PageLayout";
import { SectionCard } from "@/components/shared/SectionCard";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Skeleton } from "@/components/ui/skeleton";
import {
  IconPlayerPlay,
  IconPlayerStop,
  IconRefresh,
  IconActivity,
  IconCpu,
  IconWaveSquare,
  IconBug,
} from "@tabler/icons-react";

// ── Sparkline component ──────────────────────────────────────────────────────

const SPARKLINE_WIDTH = 120;
const SPARKLINE_HEIGHT = 36;
const SPARKLINE_SAMPLES = 60;

function Sparkline({
  history,
  min = 0,
  max = 4096,
  color = "hsl(var(--primary))",
  current,
}: {
  history: number[];
  min?: number;
  max?: number;
  color?: string;
  current?: number;
}) {
  const range = max - min || 1;
  const points = history
    .map((v, i) => {
      const x = (i / (SPARKLINE_SAMPLES - 1)) * SPARKLINE_WIDTH;
      const y = SPARKLINE_HEIGHT - ((v - min) / range) * SPARKLINE_HEIGHT;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");

  return (
    <div className="relative inline-flex flex-col items-end gap-0.5">
      <svg
        width={SPARKLINE_WIDTH}
        height={SPARKLINE_HEIGHT}
        className="overflow-visible"
        style={{ display: "block" }}
      >
        <polyline
          points={points}
          fill="none"
          stroke={color}
          strokeWidth="1.5"
          strokeLinejoin="round"
          strokeLinecap="round"
          opacity="0.9"
        />
      </svg>
      {current !== undefined && (
        <span className="text-[10px] tabular-nums text-muted-foreground leading-none">
          {current}
        </span>
      )}
    </div>
  );
}

// ── ADC key bar visualization ────────────────────────────────────────────────

function AdcBar({
  raw,
  filtered,
  max = 4096,
  keyIndex,
}: {
  raw: number;
  filtered: number;
  max?: number;
  keyIndex: number;
}) {
  const rawPct = Math.min(100, (raw / max) * 100);
  const filtPct = Math.min(100, (filtered / max) * 100);
  return (
    <div className="flex flex-col gap-0.5">
      <div className="flex items-center justify-between gap-1 mb-0.5">
        <span className="text-[10px] font-mono text-muted-foreground w-4 text-right">{keyIndex}</span>
        <div className="flex-1 h-3 bg-muted rounded-sm overflow-hidden relative">
          <div
            className="absolute inset-y-0 left-0 bg-primary/30 transition-all duration-75"
            style={{ width: `${rawPct}%` }}
          />
          <div
            className="absolute inset-y-0 left-0 bg-primary transition-all duration-75"
            style={{ width: `${filtPct}%` }}
          />
        </div>
        <span className="text-[10px] tabular-nums font-mono text-muted-foreground w-10 text-right">
          {filtered}
        </span>
      </div>
    </div>
  );
}

// ── Live ADC panel ────────────────────────────────────────────────────────────

type AdcHistory = Record<number, number[]>;

function AdcPanel({ connected }: { connected: boolean }) {
  const [running, setRunning] = useState(false);
  const [latest, setLatest] = useState<AdcDebugValues | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const tick = useCallback(async () => {
    const data = await kbheDevice.getAdcValues();
    if (!data) return;
    setLatest(data);
  }, []);

  useEffect(() => {
    if (running && connected) {
      void tick();
      intervalRef.current = setInterval(() => void tick(), 50);
    } else {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    }
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, [running, connected, tick]);

  const allKeys = latest
    ? latest.adc_raw.map((raw, i) => ({ raw, filtered: latest.adc_filtered[i] ?? 0, i }))
    : [];

  return (
    <SectionCard
      title="ADC Live View"
      description="Raw vs filtered ADC per key — ~20 Hz polling"
      headerRight={
        <div className="flex items-center gap-2">
          {latest && (
            <Badge variant="outline" className="font-mono text-[10px]">
              {latest.scan_rate_hz.toFixed(0)} Hz · {latest.scan_time_us} µs
            </Badge>
          )}
          <Button
            size="sm"
            variant={running ? "destructive" : "default"}
            className="h-7 gap-1.5"
            disabled={!connected}
            onClick={() => setRunning((r) => !r)}
          >
            {running ? (
              <><IconPlayerStop className="size-3" /> Stop</>
            ) : (
              <><IconPlayerPlay className="size-3" /> Start</>
            )}
          </Button>
        </div>
      }
    >
      {!connected ? (
        <p className="text-sm text-muted-foreground">Connect device to stream ADC data.</p>
      ) : allKeys.length === 0 ? (
        <p className="text-sm text-muted-foreground">
          {running ? "Waiting for first packet…" : "Press Start to begin streaming."}
        </p>
      ) : (
        <div className="flex flex-col gap-0">
          <div className="flex items-center gap-4 mb-3 text-[10px] text-muted-foreground">
            <span className="flex items-center gap-1">
              <span className="inline-block w-3 h-2 bg-primary/30 rounded-sm" /> Raw
            </span>
            <span className="flex items-center gap-1">
              <span className="inline-block w-3 h-2 bg-primary rounded-sm" /> Filtered
            </span>
          </div>
          <div className="grid grid-cols-1 gap-0.5">
            {allKeys.map(({ raw, filtered, i }) => (
              <AdcBar key={i} raw={raw} filtered={filtered} keyIndex={i} />
            ))}
          </div>
        </div>
      )}
    </SectionCard>
  );
}

// ── Sparkline history panel ──────────────────────────────────────────────────

function SparklinePanel({ connected }: { connected: boolean }) {
  const [running, setRunning] = useState(false);
  const [histories, setHistories] = useState<AdcHistory>({});
  const [latest, setLatest] = useState<AdcDebugValues | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const tick = useCallback(async () => {
    const data = await kbheDevice.getAdcValues();
    if (!data) return;
    setLatest(data);
    setHistories((prev) => {
      const next: AdcHistory = { ...prev };
      data.adc_filtered.forEach((v, i) => {
        const arr = next[i] ? [...next[i]] : [];
        arr.push(v);
        if (arr.length > SPARKLINE_SAMPLES) arr.shift();
        next[i] = arr;
      });
      return next;
    });
  }, []);

  useEffect(() => {
    if (running && connected) {
      void tick();
      intervalRef.current = setInterval(() => void tick(), 100);
    } else {
      if (intervalRef.current) { clearInterval(intervalRef.current); intervalRef.current = null; }
    }
    return () => { if (intervalRef.current) clearInterval(intervalRef.current); };
  }, [running, connected, tick]);

  const keys = latest ? latest.adc_filtered.map((v, i) => ({ v, i })) : [];
  const COLORS = [
    "hsl(217, 91%, 60%)", "hsl(160, 84%, 39%)", "hsl(38, 92%, 50%)",
    "hsl(0, 84%, 60%)", "hsl(280, 87%, 65%)", "hsl(199, 89%, 48%)",
  ];

  return (
    <SectionCard
      title="Travel Sparklines"
      description="Per-key filtered ADC value history (last 60 samples)"
      headerRight={
        <Button
          size="sm"
          variant={running ? "destructive" : "default"}
          className="h-7 gap-1.5"
          disabled={!connected}
          onClick={() => setRunning((r) => !r)}
        >
          {running ? <><IconPlayerStop className="size-3" /> Stop</> : <><IconPlayerPlay className="size-3" /> Start</>}
        </Button>
      }
    >
      {!connected ? (
        <p className="text-sm text-muted-foreground">Connect device to stream travel data.</p>
      ) : keys.length === 0 ? (
        <p className="text-sm text-muted-foreground">{running ? "Waiting…" : "Press Start."}</p>
      ) : (
        <div className="grid grid-cols-2 sm:grid-cols-3 gap-3">
          {keys.map(({ v, i }) => (
            <div key={i} className="flex items-center gap-2 rounded-md border px-2 py-1.5">
              <span className="text-[10px] font-mono text-muted-foreground w-4">{i}</span>
              <Sparkline
                history={histories[i] ?? [v]}
                min={0}
                max={4096}
                color={COLORS[i % COLORS.length]}
                current={v}
              />
            </div>
          ))}
        </div>
      )}
    </SectionCard>
  );
}

// ── Task timing breakdown ────────────────────────────────────────────────────

function TaskBar({ label, value, total }: { label: string; value: number; total: number }) {
  const pct = total > 0 ? Math.min(100, (value / total) * 100) : 0;
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-24 text-muted-foreground font-mono shrink-0 truncate">{label}</span>
      <div className="flex-1 h-2 bg-muted rounded-full overflow-hidden">
        <div
          className="h-full bg-primary/70 rounded-full transition-all duration-150"
          style={{ width: `${pct}%` }}
        />
      </div>
      <span className="w-14 tabular-nums text-right text-muted-foreground font-mono">
        {value} µs
      </span>
      <span className="w-8 tabular-nums text-right text-muted-foreground/60 font-mono">
        {pct.toFixed(0)}%
      </span>
    </div>
  );
}

function TimingPanel({ connected }: { connected: boolean }) {
  const [running, setRunning] = useState(false);
  const [latest, setLatest] = useState<AdcDebugValues | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const tick = useCallback(async () => {
    const data = await kbheDevice.getAdcValues();
    if (data) setLatest(data);
  }, []);

  useEffect(() => {
    if (running && connected) {
      void tick();
      intervalRef.current = setInterval(() => void tick(), 200);
    } else {
      if (intervalRef.current) { clearInterval(intervalRef.current); intervalRef.current = null; }
    }
    return () => { if (intervalRef.current) clearInterval(intervalRef.current); };
  }, [running, connected, tick]);

  const task = latest?.task_times_us;
  const analog = latest?.analog_monitor_us;
  const total = task?.total ?? 1;

  return (
    <div className="flex flex-col gap-4">
      <SectionCard
        title="Task Timing"
        description="Per-task CPU budget breakdown (µs)"
        headerRight={
          <Button
            size="sm"
            variant={running ? "destructive" : "default"}
            className="h-7 gap-1.5"
            disabled={!connected}
            onClick={() => setRunning((r) => !r)}
          >
            {running ? <><IconPlayerStop className="size-3" /> Stop</> : <><IconPlayerPlay className="size-3" /> Start</>}
          </Button>
        }
      >
        {!connected ? (
          <p className="text-sm text-muted-foreground">Connect device to view timing.</p>
        ) : !task ? (
          <p className="text-sm text-muted-foreground">
            {running ? "Waiting for extended payload…" : "Press Start. Requires firmware with extended ADC payload."}
          </p>
        ) : (
          <div className="flex flex-col gap-1.5">
            {Object.entries(task).filter(([k]) => k !== "total").map(([k, v]) => (
              <TaskBar key={k} label={k} value={v} total={total} />
            ))}
            <div className="mt-1 pt-1.5 border-t flex items-center justify-between text-xs">
              <span className="text-muted-foreground font-mono">total</span>
              <span className="tabular-nums font-mono font-medium">{total} µs</span>
            </div>
          </div>
        )}
      </SectionCard>

      {analog && (
        <SectionCard title="Analog Pipeline" description="Sub-task timing inside the analog monitor loop">
          <div className="flex flex-col gap-1.5">
            {Object.entries(analog).filter(([k]) => !["key_min","key_max","key_avg","nonzero_keys"].includes(k)).map(([k, v]) => (
              <TaskBar key={k} label={k} value={v} total={analog.store ?? 1} />
            ))}
            <div className="mt-2 pt-2 border-t grid grid-cols-2 gap-x-6 gap-y-1 text-[11px]">
              <div className="flex justify-between">
                <span className="text-muted-foreground">Key min</span>
                <span className="tabular-nums font-mono">{analog.key_min}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-muted-foreground">Key max</span>
                <span className="tabular-nums font-mono">{analog.key_max}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-muted-foreground">Key avg</span>
                <span className="tabular-nums font-mono">{analog.key_avg}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-muted-foreground">Non-zero keys</span>
                <span className="tabular-nums font-mono">{analog.nonzero_keys}</span>
              </div>
            </div>
          </div>
        </SectionCard>
      )}
    </div>
  );
}

// ── MCU metrics panel ────────────────────────────────────────────────────────

type McuHistory = { load: number[]; scanRate: number[]; temp: number[] };

function McuPanel({ connected }: { connected: boolean }) {
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [hist, setHist] = useState<McuHistory>({ load: [], scanRate: [], temp: [] });

  const mcuQ = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected,
    refetchInterval: autoRefresh && connected ? 500 : false,
  });

  useEffect(() => {
    const m = mcuQ.data;
    if (!m) return;
    setHist((prev) => {
      const push = (arr: number[], v: number) => {
        const next = [...arr, v];
        if (next.length > SPARKLINE_SAMPLES) next.shift();
        return next;
      };
      return {
        load: push(prev.load, m.load_percent),
        scanRate: push(prev.scanRate, m.scan_rate_hz),
        temp: push(prev.temp, m.temperature_valid && m.temperature_c != null ? m.temperature_c : (prev.temp.at(-1) ?? 0)),
      };
    });
  }, [mcuQ.data]);

  const m = mcuQ.data;

  const MetricCard = ({
    label, value, unit, history, min, max, color,
  }: {
    label: string; value: string; unit?: string;
    history: number[]; min: number; max: number; color?: string;
  }) => (
    <div className="rounded-lg border p-3 flex flex-col gap-2">
      <div className="flex items-start justify-between">
        <span className="text-xs text-muted-foreground">{label}</span>
        <Sparkline history={history} min={min} max={max} color={color} />
      </div>
      <div className="flex items-baseline gap-1">
        <span className="text-2xl font-semibold tabular-nums leading-none">{value}</span>
        {unit && <span className="text-xs text-muted-foreground">{unit}</span>}
      </div>
    </div>
  );

  return (
    <SectionCard
      title="MCU Metrics"
      description="Live firmware performance counters"
      headerRight={
        <div className="flex items-center gap-2">
          <span className="text-xs text-muted-foreground">Live</span>
          <Switch checked={autoRefresh} onCheckedChange={setAutoRefresh} disabled={!connected} />
          <Button
            variant="outline" size="sm" className="h-7 gap-1.5"
            disabled={!connected || mcuQ.isFetching}
            onClick={() => void mcuQ.refetch()}
          >
            <IconRefresh className="size-3" />
          </Button>
        </div>
      }
    >
      {!connected ? (
        <p className="text-sm text-muted-foreground">Connect device to view MCU metrics.</p>
      ) : mcuQ.isLoading ? (
        <div className="grid grid-cols-2 gap-3">
          {[0,1,2,3].map(i => <Skeleton key={i} className="h-20 rounded-lg" />)}
        </div>
      ) : !m ? (
        <p className="text-sm text-muted-foreground">Could not fetch MCU metrics.</p>
      ) : (
        <>
          <div className="grid grid-cols-2 gap-3">
            <MetricCard
              label="CPU Load"
              value={m.load_percent.toFixed(1)}
              unit="%"
              history={hist.load}
              min={0}
              max={100}
              color={m.load_percent > 80 ? "hsl(0,84%,60%)" : m.load_percent > 50 ? "hsl(38,92%,50%)" : "hsl(160,84%,39%)"}
            />
            <MetricCard
              label="Scan Rate"
              value={m.scan_rate_hz.toFixed(0)}
              unit="Hz"
              history={hist.scanRate}
              min={0}
              max={10000}
              color="hsl(217,91%,60%)"
            />
            <MetricCard
              label="Temperature"
              value={m.temperature_valid && m.temperature_c != null ? m.temperature_c.toFixed(1) : "—"}
              unit={m.temperature_valid ? "°C" : ""}
              history={hist.temp}
              min={0}
              max={100}
              color={
                m.temperature_valid && m.temperature_c != null && m.temperature_c > 70
                  ? "hsl(0,84%,60%)"
                  : "hsl(280,87%,65%)"
              }
            />
            <MetricCard
              label="Scan Cycle"
              value={String(m.scan_cycle_us)}
              unit="µs"
              history={[m.scan_cycle_us]}
              min={0}
              max={1000}
              color="hsl(199,89%,48%)"
            />
          </div>
          <div className="mt-3 grid grid-cols-3 gap-x-6 gap-y-1 text-xs border-t pt-3">
            <div className="flex justify-between">
              <span className="text-muted-foreground">Core clock</span>
              <span className="tabular-nums font-mono">{(m.core_clock_hz / 1_000_000).toFixed(0)} MHz</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Work</span>
              <span className="tabular-nums font-mono">{m.work_us} µs</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Vref</span>
              <span className="tabular-nums font-mono">{m.vref_mv} mV</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Load (‰)</span>
              <span className="tabular-nums font-mono">{m.load_permille}</span>
            </div>
          </div>
        </>
      )}
    </SectionCard>
  );
}

// ── Raw ADC chunk reader ─────────────────────────────────────────────────────

function RawChunkPanel({ connected }: { connected: boolean }) {
  const [startIdx, setStartIdx] = useState(0);
  const [mode, setMode] = useState<"raw" | "filtered" | "calibrated">("calibrated");
  const [data, setData] = useState<number[] | null>(null);
  const [loading, setLoading] = useState(false);

  const fetchChunk = async () => {
    setLoading(true);
    try {
      const chunk =
        mode === "raw" ? await kbheDevice.getRawAdcChunk(startIdx)
        : mode === "filtered" ? await kbheDevice.getFilteredAdcChunk(startIdx)
        : await kbheDevice.getCalibratedAdcChunk(startIdx);
      setData(chunk?.values ?? null);
    } finally {
      setLoading(false);
    }
  };

  const maxVal = data ? Math.max(...data, 1) : 4096;

  return (
    <SectionCard title="ADC Chunk Reader" description="Read a range of ADC values by start index">
      <div className="flex items-center gap-2 mb-4 flex-wrap">
        <div className="flex rounded-md overflow-hidden border text-xs">
          {(["raw", "filtered", "calibrated"] as const).map((m) => (
            <button
              key={m}
              onClick={() => setMode(m)}
              className={`px-2 py-1 capitalize transition-colors ${
                mode === m ? "bg-primary text-primary-foreground" : "hover:bg-muted"
              }`}
            >
              {m}
            </button>
          ))}
        </div>
        <div className="flex items-center gap-1.5">
          <span className="text-xs text-muted-foreground">Start index</span>
          <input
            type="number"
            min={0}
            max={255}
            value={startIdx}
            onChange={(e) => setStartIdx(Number(e.target.value))}
            className="w-14 h-7 rounded border bg-background px-1.5 text-xs tabular-nums font-mono"
          />
        </div>
        <Button
          size="sm"
          variant="outline"
          className="h-7 gap-1.5"
          disabled={!connected || loading}
          onClick={() => void fetchChunk()}
        >
          <IconRefresh className={`size-3 ${loading ? "animate-spin" : ""}`} />
          Fetch
        </Button>
      </div>
      {data === null ? (
        <p className="text-sm text-muted-foreground">No data fetched yet.</p>
      ) : data.length === 0 ? (
        <p className="text-sm text-muted-foreground">Empty response from device.</p>
      ) : (
        <div className="flex flex-col gap-0.5">
          {data.map((v, i) => (
            <div key={i} className="flex items-center gap-2 text-xs">
              <span className="w-6 text-muted-foreground font-mono text-right">{startIdx + i}</span>
              <div className="flex-1 h-2.5 bg-muted rounded-sm overflow-hidden">
                <div
                  className="h-full bg-primary transition-all"
                  style={{ width: `${(v / maxVal) * 100}%` }}
                />
              </div>
              <span className="w-10 tabular-nums font-mono text-muted-foreground text-right">{v}</span>
            </div>
          ))}
        </div>
      )}
    </SectionCard>
  );
}

// ── Page ─────────────────────────────────────────────────────────────────────

export default function Diagnostics() {
  const { status } = useDeviceSession();
  const connected = status === "connected";

  return (
    <div className="flex flex-col h-full overflow-hidden">
      <div className="shrink-0 border-b px-4 py-2 flex items-center justify-between gap-4">
        <div className="flex items-center gap-2">
          <PageHeader title="Diagnostics" description="Developer mode · live ADC, timing, MCU metrics" />
          <Badge variant="destructive" className="text-[10px] h-5">DEV</Badge>
        </div>
      </div>

      <div className="flex-1 overflow-hidden min-h-0">
        <Tabs defaultValue="adc" className="flex flex-col h-full">
          <div className="shrink-0 border-b px-4">
            <TabsList className="h-9 mt-1">
              <TabsTrigger value="adc" className="gap-1.5">
                <IconWaveSquare className="size-3" /> ADC
              </TabsTrigger>
              <TabsTrigger value="sparklines" className="gap-1.5">
                <IconActivity className="size-3" /> Travel
              </TabsTrigger>
              <TabsTrigger value="timing" className="gap-1.5">
                <IconBug className="size-3" /> Timing
              </TabsTrigger>
              <TabsTrigger value="mcu" className="gap-1.5">
                <IconCpu className="size-3" /> MCU
              </TabsTrigger>
              <TabsTrigger value="chunk" className="gap-1.5">
                <IconRefresh className="size-3" /> Chunks
              </TabsTrigger>
            </TabsList>
          </div>

          <TabsContent value="adc" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <AdcPanel connected={connected} />
            </div>
          </TabsContent>

          <TabsContent value="sparklines" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-3xl mx-auto">
              <SparklinePanel connected={connected} />
            </div>
          </TabsContent>

          <TabsContent value="timing" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <TimingPanel connected={connected} />
            </div>
          </TabsContent>

          <TabsContent value="mcu" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <McuPanel connected={connected} />
            </div>
          </TabsContent>

          <TabsContent value="chunk" className="flex-1 overflow-y-auto p-4 mt-0">
            <div className="flex flex-col gap-4 max-w-2xl mx-auto">
              <RawChunkPanel connected={connected} />
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
