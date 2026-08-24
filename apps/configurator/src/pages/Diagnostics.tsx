import { useState, useRef, useEffect, useMemo, type ReactNode } from "react";
import { useQuery } from "@tanstack/react-query";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import type { KeySettings, AdcCaptureStatus as CaptureStatusT } from "@/lib/kbhe/device";
import { KEY_COUNT, SOCD_RESOLUTION_NAMES } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { previewKeys } from "@/constants/defaultLayout";
import BaseKeyboard from "@/components/baseKeyboard";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { SectionCard, FormRow, FormRows } from "@/components/shared/SectionCard";
import { ConnectPrompt } from "@/components/shared/ConnectPrompt";
import { EmptyState } from "@/components/shared/EmptyState";
import { SegmentedControl } from "@/components/shared/SegmentedControl";
import { SliderField } from "@/components/shared/SliderField";
import { Toolbar, ToolbarDivider, ToolbarStat } from "@/components/shared/Toolbar";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Skeleton } from "@/components/ui/skeleton";
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
import { cn } from "@/lib/utils";
import {
  IconActivityHeartbeat,
  IconArrowsExchange,
  IconBolt,
  IconChartLine,
  IconCpu,
  IconCrosshair,
  IconDownload,
  IconEraser,
  IconFileExport,
  IconGauge,
  IconKeyboard,
  IconLock,
  IconPlayerPlay,
  IconPointer,
  IconRefresh,
  IconRuler,
  IconSettings2,
  IconTemperature,
  IconWaveSawTool,
  IconWaveSquare,
} from "@tabler/icons-react";

// ── Constants ────────────────────────────────────────────────────────────────

const KEY_LABELS = Array.from({ length: KEY_COUNT }, (_, i) =>
  previewKeys[i]?.baseLabel ?? String(i),
);

const MAX_TRAVEL_MM = 4.0;
const ADC_FULL_SCALE = 4095;

/** Distinct series hues that stay legible on both the light and dark surface. */
const SERIES_COLORS = [
  "oklch(0.62 0.16 250)", "oklch(0.62 0.19 25)", "oklch(0.65 0.15 150)",
  "oklch(0.72 0.16 70)", "oklch(0.60 0.18 300)", "oklch(0.64 0.19 350)",
  "oklch(0.66 0.13 195)", "oklch(0.68 0.17 45)", "oklch(0.58 0.17 275)",
  "oklch(0.70 0.16 125)", "oklch(0.68 0.13 215)", "oklch(0.62 0.20 325)",
  "oklch(0.76 0.15 95)", "oklch(0.72 0.13 175)", "oklch(0.70 0.15 55)",
];

const DATA_TYPES = ["raw", "filtered", "calibrated", "distance", "normalized"] as const;
type DataType = (typeof DATA_TYPES)[number];

const DATA_TYPE_LABELS: Record<DataType, string> = {
  raw: "Raw ADC",
  filtered: "Filtered ADC",
  calibrated: "Calibrated ADC",
  distance: "Distance (0.01 mm)",
  normalized: "Normalized (0–255)",
};

const DATA_TYPE_HINTS: Record<DataType, string> = {
  raw: "Straight off the ADC, before any processing.",
  filtered: "After the firmware noise filter.",
  calibrated: "Filtered and mapped through each key's calibration.",
  distance: "Travel depth in hundredths of a millimetre.",
  normalized: "Travel scaled to a single byte, as the HID layer sees it.",
};

const Y_DEFAULTS: Record<DataType, [number, number]> = {
  raw: [0, ADC_FULL_SCALE],
  filtered: [0, ADC_FULL_SCALE],
  calibrated: [0, ADC_FULL_SCALE],
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

/** Blue → green → yellow → red ramp, used everywhere a key is shaded by value. */
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

/** Dark text over the yellow/green half of the ramp, light text over the rest. */
function heatmapTextColor(t: number): string {
  return t > 0.42 && t < 0.92 ? "oklch(0.2 0 0)" : "oklch(0.98 0 0)";
}

function DisconnectedBanner() {
  return <ConnectPrompt feature="read live diagnostics from the firmware" />;
}

// ── Shared display pieces ────────────────────────────────────────────────────

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
    <div className="rounded-lg border bg-card px-2.5 py-2">
      <p className="truncate text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
        {label}
      </p>
      {badge ? (
        <Badge variant="outline" className="mt-1 text-[0.65rem]">
          {badge}
        </Badge>
      ) : (
        <div className="mt-1 flex items-end justify-between gap-2">
          <p className="truncate font-mono text-base font-semibold leading-none tabular-nums">
            {value}
            {unit && (
              <span className="ml-1 text-[0.7rem] font-normal text-muted-foreground">{unit}</span>
            )}
          </p>
          {trendValues && trendValues.length > 1 && (
            <Sparkline values={trendValues} className="h-5 w-14" />
          )}
        </div>
      )}
    </div>
  );
}

/** Titled run of MetricTiles — the MCU card holds 16 of them and needs grouping. */
function MetricGroup({
  title,
  children,
  columns = "sm:grid-cols-4",
}: {
  title: string;
  children: ReactNode;
  columns?: string;
}) {
  return (
    <div>
      <div className="mb-2 flex items-center gap-2">
        <h4 className="text-[0.68rem] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
          {title}
        </h4>
        <div className="h-px flex-1 bg-border" />
      </div>
      <div className={cn("grid grid-cols-2 gap-2", columns)}>{children}</div>
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
    <div className="flex items-center justify-between gap-3 border-b border-border/50 py-1.5 last:border-b-0">
      <span className="truncate text-xs text-muted-foreground">{label}</span>
      {value !== undefined ? (
        <span className="shrink-0 font-mono text-xs tabular-nums">{value}</span>
      ) : (
        <span
          className={cn(
            "inline-flex shrink-0 items-center gap-1.5 text-xs font-medium",
            on ? "text-success" : "text-muted-foreground",
          )}
        >
          <span
            className={cn(
              "size-1.5 rounded-full",
              on ? "bg-success" : "bg-muted-foreground/40",
            )}
          />
          {on == null ? "Unknown" : on ? "On" : "Off"}
        </span>
      )}
    </div>
  );
}

/** Small legend entry so charts and bar overlays explain their own colours. */
function LegendSwatch({
  color,
  shape = "line",
  children,
}: {
  color: string;
  shape?: "line" | "band";
  children: ReactNode;
}) {
  return (
    <span className="inline-flex items-center gap-1.5 text-[0.68rem] text-muted-foreground">
      <span
        className={cn("inline-block w-3 shrink-0 rounded-sm", shape === "line" ? "h-0.5" : "h-2")}
        style={{ backgroundColor: color }}
      />
      {children}
    </span>
  );
}

/**
 * The 82-key value matrix shared by the Sensors and Scope tabs. One component
 * so the two views stay identical in shape, spacing and colour scale.
 */
function KeyMatrix({
  values,
  max,
  min = 0,
  unit,
  selected,
  onToggle,
  decimals = 0,
  showValues = true,
}: {
  values: number[];
  max: number;
  min?: number;
  unit?: string;
  selected?: Set<number>;
  onToggle?: (index: number) => void;
  decimals?: number;
  showValues?: boolean;
}) {
  const interactive = Boolean(onToggle);
  const range = max - min || 1;

  return (
    <div className="grid grid-cols-6 gap-1.5 sm:grid-cols-8 xl:grid-cols-10 2xl:grid-cols-12">
      {Array.from({ length: KEY_COUNT }, (_, i) => {
        const value = values[i];
        const hasValue = typeof value === "number";
        const t = hasValue ? Math.max(0, Math.min(1, (value - min) / range)) : 0;
        const isSelected = selected?.has(i) ?? false;
        const background = hasValue ? heatmapColor(t) : undefined;

        const content = (
          <>
            <span
              className="w-full truncate text-center text-[0.6rem] font-medium leading-none"
              style={{ color: hasValue ? heatmapTextColor(t) : undefined }}
            >
              {KEY_LABELS[i]}
            </span>
            {showValues && (
              <span
                className="w-full truncate text-center font-mono text-[0.65rem] leading-none tabular-nums"
                style={{ color: hasValue ? heatmapTextColor(t) : undefined }}
              >
                {hasValue ? value.toFixed(decimals) : "—"}
                {unit && hasValue ? unit : ""}
              </span>
            )}
          </>
        );

        const className = cn(
          "flex min-h-11 flex-col items-center justify-center gap-1 rounded-md border px-1 py-1.5 transition-all",
          hasValue ? "border-transparent" : "border-border bg-muted/40 text-muted-foreground",
          interactive && "cursor-pointer hover:brightness-110",
          isSelected && "ring-2 ring-foreground ring-offset-1 ring-offset-background",
        );

        if (!interactive) {
          return (
            <div key={i} className={className} style={{ backgroundColor: background }}>
              {content}
            </div>
          );
        }

        return (
          <button
            key={i}
            type="button"
            aria-pressed={isSelected}
            title={`${KEY_LABELS[i]} (#${i})`}
            onClick={() => onToggle?.(i)}
            className={cn(
              className,
              "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring",
            )}
            style={{ backgroundColor: background }}
          >
            {content}
          </button>
        );
      })}
    </div>
  );
}

/** Colour scale legend for the key matrix and the keyboard heatmap. */
function HeatScale({ min, max, unit }: { min: string; max: string; unit?: string }) {
  return (
    <div className="flex items-center gap-2">
      <span className="font-mono text-[0.65rem] tabular-nums text-muted-foreground">
        {min}
        {unit}
      </span>
      <div
        className="h-1.5 w-24 rounded-full"
        style={{
          background: `linear-gradient(to right, ${[0, 0.25, 0.5, 0.75, 1]
            .map((t) => heatmapColor(t))
            .join(", ")})`,
        }}
      />
      <span className="font-mono text-[0.65rem] tabular-nums text-muted-foreground">
        {max}
        {unit}
      </span>
    </div>
  );
}

// ── Charts ───────────────────────────────────────────────────────────────────

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
      className="w-full rounded-lg border bg-surface-sunken text-foreground"
      preserveAspectRatio="none"
      style={{ height: CHART_H }}
    >
      {Array.from({ length: gridLines + 1 }, (_, i) => {
        const y = (i / gridLines) * CHART_H;
        const val = yMax - (i / gridLines) * range;
        return (
          <g key={i}>
            <line x1={0} y1={y} x2={CHART_W} y2={y} stroke="currentColor" strokeOpacity={0.1} />
            <text
              x={4}
              y={y - 3}
              fontSize={10}
              fill="currentColor"
              opacity={0.45}
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
            stroke={colors[+keyIdx] ?? "currentColor"}
            strokeWidth={1.5}
            strokeLinejoin="round"
            vectorEffect="non-scaling-stroke"
          />
        );
      })}
    </svg>
  );
}

function XYChart({
  measurements,
  yMin,
  yMax,
  xMin,
  xMax,
}: {
  measurements: { distance: number; rawAdc: number }[];
  yMin: number;
  yMax: number;
  xMin: number;
  xMax: number;
}) {
  const xRange = xMax - xMin || 1;
  const yRange = yMax - yMin || 1;
  const gridLines = 5;

  const pts = measurements
    .map((m) => {
      const x = ((m.distance - xMin) / xRange) * CHART_W;
      const y = CHART_H - ((m.rawAdc - yMin) / yRange) * CHART_H;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");

  return (
    <svg
      viewBox={`0 0 ${CHART_W} ${CHART_H}`}
      className="w-full rounded-lg border bg-surface-sunken text-foreground"
      preserveAspectRatio="none"
      style={{ height: CHART_H }}
    >
      {Array.from({ length: gridLines + 1 }, (_, i) => {
        const y = (i / gridLines) * CHART_H;
        const val = yMax - (i / gridLines) * yRange;
        return (
          <g key={`y-${i}`}>
            <line x1={0} y1={y} x2={CHART_W} y2={y} stroke="currentColor" strokeOpacity={0.1} />
            <text x={4} y={y - 3} fontSize={10} fill="currentColor" opacity={0.45} fontFamily="monospace">
              {val.toFixed(0)}
            </text>
          </g>
        );
      })}
      {Array.from({ length: gridLines + 1 }, (_, i) => {
        const x = (i / gridLines) * CHART_W;
        const val = xMin + (i / gridLines) * xRange;
        return (
          <g key={`x-${i}`}>
            <line x1={x} y1={0} x2={x} y2={CHART_H} stroke="currentColor" strokeOpacity={0.1} />
            <text x={x + 3} y={CHART_H - 5} fontSize={10} fill="currentColor" opacity={0.45} fontFamily="monospace">
              {val.toFixed(1)} mm
            </text>
          </g>
        );
      })}

      {measurements.length > 1 && (
        <polyline
          points={pts}
          fill="none"
          stroke={SERIES_COLORS[0]}
          strokeWidth={2}
          strokeLinejoin="round"
          vectorEffect="non-scaling-stroke"
        />
      )}

      {measurements.map((m, i) => {
        const x = ((m.distance - xMin) / xRange) * CHART_W;
        const y = CHART_H - ((m.rawAdc - yMin) / yRange) * CHART_H;
        return (
          <circle key={i} cx={x} cy={y} r={3} fill={SERIES_COLORS[0]} />
        );
      })}
    </svg>
  );
}

// ── Vitals bar ───────────────────────────────────────────────────────────────

function Vital({
  icon,
  label,
  value,
  unit,
  tone = "default",
}: {
  icon: ReactNode;
  label: string;
  value: string;
  unit?: string;
  tone?: "default" | "success" | "warning" | "danger";
}) {
  return (
    <div className="flex min-w-0 items-center gap-2">
      <span
        className={cn(
          "flex size-7 shrink-0 items-center justify-center rounded-md [&_svg]:size-3.5",
          tone === "default" && "bg-muted text-muted-foreground",
          tone === "success" && "bg-success/12 text-success",
          tone === "warning" && "bg-warning/15 text-warning",
          tone === "danger" && "bg-destructive/10 text-destructive",
        )}
      >
        {icon}
      </span>
      <div className="min-w-0">
        <p className="truncate text-[0.62rem] font-medium uppercase tracking-[0.06em] text-muted-foreground">
          {label}
        </p>
        <p className="truncate font-mono text-xs font-semibold leading-tight tabular-nums">
          {value}
          {unit && <span className="ml-0.5 font-normal text-muted-foreground">{unit}</span>}
        </p>
      </div>
    </div>
  );
}

/**
 * Always-visible device vitals. Previously these lived inside one tab, so the
 * first question a diagnostics page has to answer — is the board healthy right
 * now — was three clicks away from four of the five views.
 */
function VitalsBar({ connected }: { connected: boolean }) {
  const mcuQ = useQuery({
    queryKey: queryKeys.device.mcuMetrics(),
    queryFn: () => kbheDevice.getMcuMetrics(),
    enabled: connected,
    refetchInterval: connected ? 500 : false,
  });

  const mcu = mcuQ.data;
  const load = mcu?.load_percent ?? null;
  const temp = mcu?.temperature_valid ? mcu?.temperature_c ?? null : null;

  return (
    <div className="flex shrink-0 flex-wrap items-center gap-x-6 gap-y-3 border-b bg-surface-sunken/50 px-5 py-2.5">
      <Vital
        icon={<IconActivityHeartbeat />}
        label="Link"
        value={connected ? "Live" : "Offline"}
        tone={connected ? "success" : "default"}
      />
      <Vital
        icon={<IconWaveSquare />}
        label="Scan rate"
        value={mcu ? String(mcu.scan_rate_hz) : "—"}
        unit={mcu ? "Hz" : undefined}
      />
      <Vital
        icon={<IconGauge />}
        label="CPU load"
        value={load != null ? load.toFixed(1) : "—"}
        unit={load != null ? "%" : undefined}
        tone={load == null ? "default" : load > 80 ? "danger" : load > 55 ? "warning" : "success"}
      />
      <Vital
        icon={<IconTemperature />}
        label="Temperature"
        value={temp != null ? temp.toFixed(1) : "—"}
        unit={temp != null ? "°C" : undefined}
        tone={temp == null ? "default" : temp > 70 ? "danger" : temp > 55 ? "warning" : "default"}
      />
      <Vital
        icon={<IconBolt />}
        label="Vref"
        value={mcu ? (mcu.vref_mv / 1000).toFixed(3) : "—"}
        unit={mcu ? "V" : undefined}
      />
      <Vital
        icon={<IconCpu />}
        label="Scan cycle"
        value={mcu ? String(mcu.scan_cycle_us) : "—"}
        unit={mcu ? "µs" : undefined}
      />
    </div>
  );
}

// ── Live tab: keyboard heatmap + per-key travel inspector ───────────────────

function TravelBar({
  index,
  distance,
  actuated,
  settings,
}: {
  index: number;
  distance: number;
  actuated: boolean;
  settings?: KeySettings;
}) {
  const barH = 190;
  const toPx = (mm: number) => Math.max(0, Math.min(barH, (mm / MAX_TRAVEL_MM) * barH));

  return (
    <div className="flex w-12 shrink-0 flex-col items-center gap-1.5">
      <span className="w-full truncate text-center text-[0.65rem] font-medium" title={KEY_LABELS[index]}>
        {KEY_LABELS[index]}
      </span>

      <div
        className={cn(
          "relative w-9 overflow-hidden rounded-md border bg-surface-sunken transition-colors",
          actuated && "border-success/60",
        )}
        style={{ height: barH }}
      >
        {/* Travel fill grows downward from the top, matching key motion. */}
        <div
          className={cn(
            "absolute inset-x-0 top-0 transition-[height] duration-75",
            actuated ? "bg-success/25" : "bg-foreground/12",
          )}
          style={{ height: `${(distance / MAX_TRAVEL_MM) * 100}%` }}
        />

        {settings?.rapid_trigger_enabled && (
          <>
            <div
              className="absolute inset-x-0 bg-chart-4/25"
              style={{
                top: toPx(distance),
                height: toPx(distance + settings.rapid_trigger_press) - toPx(distance),
              }}
              title={`Rapid trigger press window: ${settings.rapid_trigger_press} mm`}
            />
            <div
              className="absolute inset-x-0 bg-warning/25"
              style={{
                top: toPx(Math.max(0, distance - settings.rapid_trigger_release)),
                height:
                  toPx(distance) - toPx(Math.max(0, distance - settings.rapid_trigger_release)),
              }}
              title={`Rapid trigger release window: ${settings.rapid_trigger_release} mm`}
            />
          </>
        )}

        {settings && (
          <>
            <div
              className="absolute inset-x-0 z-10 h-px bg-success"
              style={{ top: toPx(settings.actuation_point_mm) }}
              title={`Actuation: ${settings.actuation_point_mm} mm`}
            />
            <div
              className="absolute inset-x-0 z-10 h-px bg-destructive"
              style={{ top: toPx(settings.release_point_mm) }}
              title={`Release: ${settings.release_point_mm} mm`}
            />
          </>
        )}

        {/* Current position rides on top of every marker. */}
        <div
          className="absolute inset-x-0 z-20 h-0.5 bg-foreground transition-[top] duration-75"
          style={{ top: toPx(distance) }}
        />
      </div>

      <span className="font-mono text-[0.65rem] tabular-nums">{distance.toFixed(2)}</span>
      <span
        className={cn(
          "rounded px-1 py-px text-[0.6rem] font-medium",
          actuated ? "bg-success/15 text-success" : "bg-muted text-muted-foreground",
        )}
      >
        {actuated ? "down" : "up"}
      </span>
    </div>
  );
}

function LiveTab({ connected, active }: { connected: boolean; active: boolean }) {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const clearSelection = useKeyboardStore((s) => s.clearSelection);

  const keyStatesQ = useQuery({
    queryKey: queryKeys.diagnostics.keyStates(),
    queryFn: () => kbheDevice.getKeyStates(),
    enabled: connected && active,
    refetchInterval: connected && active ? 100 : false,
  });

  // Thresholds used to be behind a "Reload Thresholds" button, so the markers
  // the inspector exists to show were missing until you found it.
  const settingsQ = useQuery({
    queryKey: queryKeys.diagnostics.allKeySettings(),
    queryFn: () => kbheDevice.getAllKeySettings(),
    enabled: connected && active,
    staleTime: 30_000,
  });

  const distances = useMemo(() => keyStatesQ.data?.distances_mm ?? [], [keyStatesQ.data]);
  const states = keyStatesQ.data?.states ?? [];

  const selectedArr = useMemo(() => {
    const s = new Set<number>();
    for (const k of selectedKeys) {
      if (k.startsWith("key-")) s.add(parseInt(k.replace("key-", ""), 10));
    }
    return Array.from(s).sort((a, b) => a - b);
  }, [selectedKeys]);

  if (!connected) return <DisconnectedBanner />;

  const keyColorMap: Record<string, string> = {};
  for (let i = 0; i < KEY_COUNT; i++) {
    const dist = distances[i] ?? 0;
    keyColorMap[`key-${i}`] = heatmapColor(Math.min(dist / MAX_TRAVEL_MM, 1));
  }

  const renderKeyOverlay = (keyId: string) => {
    if (!keyId.startsWith("key-")) return undefined;
    const idx = parseInt(keyId.replace("key-", ""), 10);
    const dist = distances[idx] ?? 0;
    const t = Math.min(dist / MAX_TRAVEL_MM, 1);
    return (
      <span
        className="font-mono text-[8px] tabular-nums"
        style={{ color: heatmapTextColor(t) }}
      >
        {dist.toFixed(1)}
      </span>
    );
  };

  const pressedCount = states.filter(Boolean).length;

  return (
    <div className="flex flex-col gap-5">
      <SectionCard
        title="Travel heatmap"
        description="Every key shaded by how far it is currently pressed. Click keys to inspect them below."
        icon={<IconKeyboard />}
        headerRight={
          <>
            <HeatScale min="0" max={String(MAX_TRAVEL_MM)} unit=" mm" />
            <ToolbarStat
              label="Pressed"
              value={String(pressedCount)}
              tone={pressedCount > 0 ? "active" : "default"}
            />
          </>
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

      <SectionCard
        title="Key inspector"
        description="Live travel against each key's actuation, release and rapid-trigger windows."
        icon={<IconRuler />}
        headerRight={
          selectedArr.length > 0 ? (
            <>
              <ToolbarStat
                label="Inspecting"
                value={`${selectedArr.length} ${selectedArr.length === 1 ? "key" : "keys"}`}
                tone="active"
              />
              <Button variant="ghost" size="sm" onClick={clearSelection}>
                Clear
              </Button>
            </>
          ) : undefined
        }
        footer={
          selectedArr.length > 0 ? (
            <div className="mr-auto flex flex-wrap items-center gap-x-4 gap-y-1.5">
              <LegendSwatch color="var(--foreground)">Current position</LegendSwatch>
              <LegendSwatch color="var(--success)">Actuation point</LegendSwatch>
              <LegendSwatch color="var(--destructive)">Release point</LegendSwatch>
              <LegendSwatch color="color-mix(in oklab, var(--chart-4) 40%, transparent)" shape="band">
                RT press window
              </LegendSwatch>
              <LegendSwatch color="color-mix(in oklab, var(--warning) 40%, transparent)" shape="band">
                RT release window
              </LegendSwatch>
            </div>
          ) : undefined
        }
      >
        {selectedArr.length === 0 ? (
          <EmptyState
            icon={<IconPointer />}
            title="No keys selected"
            description="Click one or more keys in the heatmap above to watch their travel against the configured thresholds."
          />
        ) : (
          <div className="flex gap-3 overflow-x-auto pb-1">
            {selectedArr.map((idx) => (
              <TravelBar
                key={idx}
                index={idx}
                distance={distances[idx] ?? 0}
                actuated={Boolean(states[idx])}
                settings={settingsQ.data?.find((s) => s.key_index === idx)}
              />
            ))}
          </div>
        )}
      </SectionCard>
    </div>
  );
}

// ── Sensors tab: raw ADC across the whole board ─────────────────────────────

function SensorsTab({ connected, active }: { connected: boolean; active: boolean }) {
  const [source, setSource] = useState<"raw" | "filtered" | "calibrated">("raw");

  const valuesQ = useQuery({
    queryKey: ["diagnostics", "sensorMatrix", source],
    queryFn: async () => {
      switch (source) {
        case "filtered":
          return kbheDevice.getAllFilteredAdcValues();
        case "calibrated":
          return kbheDevice.getAllCalibratedAdcValues();
        default:
          return kbheDevice.getAllRawAdcValues();
      }
    },
    enabled: connected && active,
    refetchInterval: connected && active ? 120 : false,
  });

  const values = useMemo(() => valuesQ.data ?? [], [valuesQ.data]);
  const stats = useMemo(() => {
    if (values.length === 0) return null;
    const min = Math.min(...values);
    const max = Math.max(...values);
    const avg = Math.round(values.reduce((a, b) => a + b, 0) / values.length);
    const spread = max - min;
    return { min, max, avg, spread };
  }, [values]);

  if (!connected) return <DisconnectedBanner />;

  return (
    <div className="flex flex-col gap-5">
      <Toolbar
        left={
          <>
            <span className="text-xs text-muted-foreground">Source</span>
            <SegmentedControl
              aria-label="ADC source"
              value={source}
              onChange={setSource}
              options={[
                { value: "raw", label: "Raw", title: "Straight off the ADC" },
                { value: "filtered", label: "Filtered", title: "After the firmware noise filter" },
                { value: "calibrated", label: "Calibrated", title: "Mapped through per-key calibration" },
              ]}
            />
            <ToolbarDivider />
            <HeatScale min="0" max={String(ADC_FULL_SCALE)} />
          </>
        }
        right={
          <Button
            variant="outline"
            size="sm"
            disabled={valuesQ.isFetching}
            onClick={() => void valuesQ.refetch()}
          >
            <IconRefresh className={cn("size-3.5", valuesQ.isFetching && "animate-spin")} />
            Refresh
          </Button>
        }
      />

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <MetricTile label="Minimum" value={stats ? String(stats.min) : "—"} />
        <MetricTile label="Maximum" value={stats ? String(stats.max) : "—"} />
        <MetricTile label="Average" value={stats ? String(stats.avg) : "—"} />
        <MetricTile label="Spread" value={stats ? String(stats.spread) : "—"} />
      </div>

      <SectionCard
        title="Per-key readings"
        description={`All ${KEY_COUNT} sensors, polled continuously while this tab is open.`}
        icon={<IconWaveSquare />}
      >
        {values.length === 0 ? (
          <div className="grid grid-cols-6 gap-1.5 sm:grid-cols-8 xl:grid-cols-10 2xl:grid-cols-12">
            {Array.from({ length: KEY_COUNT }, (_, i) => (
              <Skeleton key={i} className="h-11 rounded-md" />
            ))}
          </div>
        ) : (
          <KeyMatrix values={values} max={ADC_FULL_SCALE} />
        )}
      </SectionCard>
    </div>
  );
}

// ── Scope tab: rolling time series for selected keys ────────────────────────

function ScopeTab({ connected, active }: { connected: boolean; active: boolean }) {
  const [dtype, setDtype] = useState<DataType>("filtered");
  const [graphKeys, setGraphKeys] = useState<number[]>([]);
  const [depth, setDepth] = useState(200);
  const [yMin, setYMin] = useState(0);
  const [yMax, setYMax] = useState(ADC_FULL_SCALE);
  const [buffers, setBuffers] = useState<Record<number, number[]>>({});
  const [paused, setPaused] = useState(false);
  const lastUpdate = useRef(0);

  useEffect(() => {
    const [lo, hi] = Y_DEFAULTS[dtype];
    setYMin(lo);
    setYMax(hi);
  }, [dtype]);

  const streaming = connected && active && !paused && graphKeys.length > 0;

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
    enabled: streaming,
    refetchInterval: streaming ? 50 : false,
  });

  useEffect(() => {
    const data = dataQ.data;
    if (!data || paused || dataQ.dataUpdatedAt === lastUpdate.current) return;
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
  }, [dataQ.data, dataQ.dataUpdatedAt, graphKeys, depth, paused]);

  const toggleGraphKey = (idx: number) => {
    setGraphKeys((prev) =>
      prev.includes(idx) ? prev.filter((k) => k !== idx) : [...prev, idx],
    );
  };

  const colors = useMemo(
    () =>
      Object.fromEntries(
        graphKeys.map((k, i) => [k, SERIES_COLORS[i % SERIES_COLORS.length]]),
      ),
    [graphKeys],
  );

  const heatValues = dataQ.data ?? [];
  const [heatLo, heatHi] = Y_DEFAULTS[dtype];

  const autoScaleY = () => {
    const allVals = Object.values(buffers).flat();
    if (allVals.length === 0) return;
    const lo = Math.min(...allVals);
    const hi = Math.max(...allVals);
    const margin = (hi - lo) * 0.1 || 10;
    setYMin(Math.floor(lo - margin));
    setYMax(Math.ceil(hi + margin));
  };

  const firstSeries = graphKeys.length > 0 ? buffers[graphKeys[0]] : undefined;

  if (!connected) return <DisconnectedBanner />;

  return (
    <div className="flex flex-col gap-5">
      <Toolbar
        left={
          <>
            <span className="text-xs text-muted-foreground">Signal</span>
            <Select
              value={dtype}
              items={DATA_TYPES.map((t) => ({ value: t, label: DATA_TYPE_LABELS[t] }))}
              onValueChange={(v) => {
                setDtype(v as DataType);
                setBuffers({});
              }}
            >
              <SelectTrigger size="sm" className="w-48 text-xs">
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
            <ToolbarDivider />
            <ToolbarStat
              label="Tracing"
              value={`${graphKeys.length} ${graphKeys.length === 1 ? "key" : "keys"}`}
              tone={graphKeys.length > 0 ? "active" : "default"}
            />
          </>
        }
        right={
          <>
            <Button
              variant={paused ? "default" : "outline"}
              size="sm"
              disabled={graphKeys.length === 0}
              onClick={() => setPaused((p) => !p)}
            >
              <IconPlayerPlay className="size-3.5" />
              {paused ? "Resume" : "Pause"}
            </Button>
            <Button variant="outline" size="sm" onClick={() => setBuffers({})}>
              <IconEraser className="size-3.5" />
              Clear
            </Button>
          </>
        }
      />

      <SectionCard
        title="Time series"
        description={DATA_TYPE_HINTS[dtype]}
        icon={<IconChartLine />}
        headerRight={
          firstSeries && firstSeries.length > 0 ? (
            <div className="flex gap-3 rounded-md border bg-muted/40 px-2.5 py-1 font-mono text-[0.68rem] tabular-nums">
              <span className="text-muted-foreground">
                cur <span className="text-foreground">{firstSeries[firstSeries.length - 1]?.toFixed(1)}</span>
              </span>
              <span className="text-muted-foreground">
                min <span className="text-foreground">{Math.min(...firstSeries).toFixed(1)}</span>
              </span>
              <span className="text-muted-foreground">
                max <span className="text-foreground">{Math.max(...firstSeries).toFixed(1)}</span>
              </span>
              <span className="text-muted-foreground">
                avg <span className="text-foreground">
                  {(firstSeries.reduce((a, b) => a + b, 0) / firstSeries.length).toFixed(1)}
                </span>
              </span>
            </div>
          ) : undefined
        }
      >
        {graphKeys.length === 0 ? (
          <EmptyState
            icon={<IconChartLine />}
            title="No keys traced yet"
            description="Pick keys in the matrix below and their signal will start scrolling here."
          />
        ) : (
          <div className="flex flex-col gap-3">
            <TimeChart
              buffers={buffers}
              depth={depth}
              yMin={yMin}
              yMax={yMax}
              colors={colors}
            />

            <div className="flex flex-wrap gap-1.5">
              {graphKeys.map((k, i) => (
                <button
                  key={k}
                  type="button"
                  onClick={() => toggleGraphKey(k)}
                  title="Remove from trace"
                  className="inline-flex items-center gap-1.5 rounded-full border bg-card px-2 py-0.5 text-[0.68rem] font-medium transition-colors hover:bg-muted"
                >
                  <span
                    className="size-2 shrink-0 rounded-full"
                    style={{ backgroundColor: SERIES_COLORS[i % SERIES_COLORS.length] }}
                  />
                  {KEY_LABELS[k]}
                  <span className="font-mono text-muted-foreground">#{k}</span>
                </button>
              ))}
            </div>
          </div>
        )}
      </SectionCard>

      <SectionCard
        title="Capture window"
        description="How much history the chart keeps, and the vertical range it plots."
        icon={<IconWaveSawTool />}
      >
        <div className="grid gap-5 lg:grid-cols-2">
          <SliderField
            label="History depth"
            description="Number of samples kept per key, at roughly 20 samples per second."
            min={50}
            max={2000}
            step={50}
            unit="samples"
            value={depth}
            onCommit={setDepth}
          />
          <FormRows>
            <FormRow
              label="Vertical range"
              description="Clamp the chart to a fixed band, or fit it to what has been captured."
            >
              <Input
                type="number"
                aria-label="Minimum Y value"
                value={yMin}
                onChange={(e) => setYMin(+e.target.value)}
                className="h-8 w-20 font-mono text-xs"
              />
              <span className="text-xs text-muted-foreground">to</span>
              <Input
                type="number"
                aria-label="Maximum Y value"
                value={yMax}
                onChange={(e) => setYMax(+e.target.value)}
                className="h-8 w-20 font-mono text-xs"
              />
              <Button variant="outline" size="sm" onClick={autoScaleY}>
                Fit
              </Button>
            </FormRow>
          </FormRows>
        </div>
      </SectionCard>

      <SectionCard
        title="Key selection"
        description="Click a key to add or remove it from the trace. Shading shows its live value."
        icon={<IconKeyboard />}
        headerRight={
          <>
            <HeatScale min={String(heatLo)} max={String(heatHi)} />
            {graphKeys.length > 0 && (
              <Button variant="ghost" size="sm" onClick={() => setGraphKeys([])}>
                Deselect all
              </Button>
            )}
          </>
        }
      >
        <KeyMatrix
          values={heatValues}
          min={heatLo}
          max={heatHi}
          selected={new Set(graphKeys)}
          onToggle={toggleGraphKey}
          showValues={false}
        />
      </SectionCard>
    </div>
  );
}

// ── System tab: firmware counters, configuration and low-level tools ────────

function SystemTab({ connected, active }: { connected: boolean; active: boolean }) {
  const { firmwareVersion } = useDeviceSession();

  const adcQ = useQuery({
    queryKey: queryKeys.diagnostics.adcValues(),
    queryFn: () => kbheDevice.getAdcValues(),
    enabled: connected && active,
    refetchInterval: connected && active ? 250 : false,
  });

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
    if (!metrics) return;

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

  const lockQ = useQuery({
    queryKey: queryKeys.device.lockStates(),
    queryFn: () => kbheDevice.getLockStates(),
    enabled: connected && active,
    refetchInterval: connected && active ? 1000 : false,
  });

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

  // ── ADC filter ──
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
  const [savingFilter, setSavingFilter] = useState(false);

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

  const filterDirty =
    (filterEnabledQ.data != null && filterEnabled !== filterEnabledQ.data)
    || (filterParamsQ.data != null
      && (noise !== filterParamsQ.data.noise_band
        || alphaMin !== filterParamsQ.data.alpha_min_denom
        || alphaMax !== filterParamsQ.data.alpha_max_denom));

  const saveFilter = async () => {
    setSavingFilter(true);
    try {
      if (filterEnabled != null) await kbheDevice.setFilterEnabled(filterEnabled);
      await kbheDevice.setFilterParams(noise, alphaMin, alphaMax);
      await Promise.all([filterEnabledQ.refetch(), filterParamsQ.refetch()]);
    } finally {
      setSavingFilter(false);
    }
  };

  // ── ADC capture ──
  const [captureKey, setCaptureKey] = useState(0);
  const [captureDuration, setCaptureDuration] = useState(500);
  const [captureStatus, setCaptureStatus] = useState<CaptureStatusT | null>(null);
  const [captureData, setCaptureData] = useState<{ raw: number[]; filtered: number[] } | null>(null);
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

  // ── SOCD pairs ──
  const allSettingsQ = useQuery({
    queryKey: queryKeys.diagnostics.allKeySettings(),
    queryFn: () => kbheDevice.getAllKeySettings(),
    enabled: connected && active,
    staleTime: 30_000,
  });

  const socdPairs = useMemo(() => {
    const settings = allSettingsQ.data;
    if (!settings) return [];
    const seen = new Set<string>();
    const pairs: { a: number; b: number; resolution: number }[] = [];
    for (const ks of settings) {
      if (ks.socd_pair == null) continue;
      const key = `${Math.min(ks.key_index, ks.socd_pair)}-${Math.max(ks.key_index, ks.socd_pair)}`;
      if (seen.has(key)) continue;
      seen.add(key);
      pairs.push({ a: ks.key_index, b: ks.socd_pair, resolution: ks.socd_resolution });
    }
    return pairs;
  }, [allSettingsQ.data]);

  if (!connected) return <DisconnectedBanner />;

  const adc = adcQ.data;
  const mcu = mcuQ.data;
  const locks = lockQ.data;
  const opts = optionsQ.data;

  return (
    <div className="flex flex-col gap-5">
      <SectionCard
        title="MCU counters"
        description="Health, scan timing and flash persistence, straight from the firmware."
        icon={<IconCpu />}
        headerRight={
          <Button
            size="sm"
            variant="outline"
            onClick={() => void mcuQ.refetch()}
            title="Re-read metrics"
          >
            <IconRefresh className="size-3.5" />
            Refresh
          </Button>
        }
      >
        {!mcu ? (
          <Skeleton className="h-20" />
        ) : (
          <div className="flex flex-col gap-4">
            <MetricGroup title="Health">
              <MetricTile
                label="Temperature"
                value={
                  mcu.temperature_valid && mcu.temperature_c != null
                    ? mcu.temperature_c.toFixed(1)
                    : "—"
                }
                unit={mcu.temperature_valid ? "°C" : ""}
                trendValues={mcuTrends.temperature}
              />
              <MetricTile
                label="Vref"
                value={String(mcu.vref_mv)}
                unit="mV"
                trendValues={mcuTrends.vref}
              />
              <MetricTile
                label="Core clock"
                value={(mcu.core_clock_hz / 1e6).toFixed(0)}
                unit="MHz"
                trendValues={mcuTrends.coreClock}
              />
              <MetricTile
                label="CPU load"
                value={mcu.load_percent.toFixed(1)}
                unit="%"
                trendValues={mcuTrends.loadPercent}
              />
            </MetricGroup>

            <MetricGroup title="Scan timing">
              <MetricTile
                label="Scan rate"
                value={String(mcu.scan_rate_hz)}
                unit="Hz"
                trendValues={mcuTrends.scanRate}
              />
              <MetricTile
                label="Scan cycle"
                value={String(mcu.scan_cycle_us)}
                unit="µs"
                trendValues={mcuTrends.scanCycle}
              />
              <MetricTile
                label="Work per cycle"
                value={String(mcu.work_us)}
                unit="µs"
                trendValues={mcuTrends.work}
              />
              <MetricTile
                label="Load"
                value={String(mcu.load_permille)}
                unit="‰"
                trendValues={mcuTrends.loadPermille}
              />
              {mcu.realtime_persistence_metrics_available && (
                <>
                  <MetricTile label="p99 scan" value={String(mcu.p99_scan_cycle_us)} unit="µs" />
                  <MetricTile label="Max scan" value={String(mcu.max_scan_cycle_us)} unit="µs" />
                  <MetricTile
                    label="Missed deadlines"
                    value={String(mcu.scan_deadline_miss_count)}
                  />
                  <MetricTile
                    label="8 kHz guarantee"
                    value={mcu.flash_hard_8khz_guarantee ? "Hard" : "Limited"}
                    unit={
                      mcu.flash_hard_8khz_guarantee
                        ? ""
                        : `flash max ${mcu.flash_word_program_datasheet_max_us} µs`
                    }
                  />
                </>
              )}
            </MetricGroup>

            {mcu.realtime_persistence_metrics_available && (
              <MetricGroup title="Flash persistence">
                <MetricTile label="Words written" value={String(mcu.flash_programmed_words)} />
                <MetricTile label="Runtime erases" value={String(mcu.flash_runtime_erase_count)} />
                <MetricTile
                  label="No-space deferrals"
                  value={String(mcu.flash_deferred_no_space_count)}
                />
                <MetricTile
                  label="Write budget"
                  value={String(mcu.flash_max_words_per_step)}
                  unit="word/scan"
                />
              </MetricGroup>
            )}

            <MetricGroup title="Keyboard transport">
              <MetricTile
                label="6KRO queue peak"
                value={String(mcu.keyboard_queue_high_watermark)}
                unit="reports"
              />
              <MetricTile
                label="NKRO queue peak"
                value={String(mcu.nkro_queue_high_watermark)}
                unit="reports"
              />
              <MetricTile
                label="Queue overflows"
                value={String(
                  mcu.keyboard_queue_overflow_count_sat
                  + mcu.nkro_queue_overflow_count_sat,
                )}
              />
              <MetricTile
                label="Failed transfers"
                value={String(mcu.keyboard_transfer_failed_count_sat)}
              />
            </MetricGroup>
          </div>
        )}
      </SectionCard>

      <SectionCard
        title="Scan loop"
        description="Per-task timing reported by the ADC endpoint."
        icon={<IconActivityHeartbeat />}
      >
        {!adc ? (
          <Skeleton className="h-16" />
        ) : (
          <div className="flex flex-col gap-4">
            <MetricGroup title="Rates">
              <MetricTile label="Scan rate" value={String(adc.scan_rate_hz)} unit="Hz" />
              <MetricTile label="Scan time" value={String(adc.scan_time_us)} unit="µs" />
              <MetricTile label="Payload" badge={adc.adc_payload_format} />
              <MetricTile label="Protocol" badge={adc.task_times_us ? "Extended" : "Legacy"} />
            </MetricGroup>

            {adc.task_times_us && (
              <MetricGroup title="Task times (µs)">
                {Object.entries(adc.task_times_us).map(([k, v]) => (
                  <div
                    key={k}
                    className="flex items-center justify-between gap-2 rounded-lg border bg-card px-2.5 py-1.5"
                  >
                    <span className="truncate font-mono text-[0.7rem] text-muted-foreground">{k}</span>
                    <span className="shrink-0 font-mono text-xs tabular-nums">{v}</span>
                  </div>
                ))}
              </MetricGroup>
            )}

            {adc.analog_monitor_us && (
              <MetricGroup title="Analog monitor">
                {Object.entries(adc.analog_monitor_us).map(([k, v]) => (
                  <div
                    key={k}
                    className="flex items-center justify-between gap-2 rounded-lg border bg-card px-2.5 py-1.5"
                  >
                    <span className="truncate font-mono text-[0.7rem] text-muted-foreground">{k}</span>
                    <span className="shrink-0 font-mono text-xs tabular-nums">{v}</span>
                  </div>
                ))}
              </MetricGroup>
            )}
          </div>
        )}
      </SectionCard>

      <div className="grid grid-cols-1 gap-4 lg:grid-cols-[minmax(0,1fr)_minmax(0,1.4fr)]">
        <SectionCard
          title="Lock states"
          description="Indicator LEDs the host is currently asserting."
          icon={<IconLock />}
        >
          {!locks ? (
            <Skeleton className="h-8" />
          ) : (
            <div className="flex flex-col">
              <ConfigRow label="Caps Lock" on={locks.caps_lock} />
              <ConfigRow label="Num Lock" on={locks.num_lock} />
              <ConfigRow label="Scroll Lock" on={locks.scroll_lock} />
            </div>
          )}
        </SectionCard>

        <SectionCard
          title="Configuration snapshot"
          description="What the firmware currently reports about itself."
          icon={<IconSettings2 />}
        >
          <div className="grid grid-cols-1 gap-x-6 sm:grid-cols-2">
            <ConfigRow label="Firmware" value={firmwareVersion ?? "—"} />
            <ConfigRow label="Keyboard" on={opts?.keyboard_enabled} />
            <ConfigRow label="Gamepad" on={opts?.gamepad_enabled} />
            <ConfigRow label="NKRO" on={nkroQ.data == null ? undefined : nkroQ.data} />
            <ConfigRow label="Raw HID echo" on={opts?.raw_hid_echo} />
            <ConfigRow label="LED thermal protection" on={opts?.led_thermal_protection_enabled} />
            {gamepadQ.data && (
              <ConfigRow
                label="Gamepad API"
                value={gamepadQ.data.api_mode === 0 ? "DirectInput" : "XInput"}
              />
            )}
          </div>
        </SectionCard>
      </div>

      <SectionCard
        title="ADC noise filter"
        description="Low-level filter coefficients. The Performance page exposes the same settings with plain-language labels."
        icon={<IconWaveSawTool />}
        headerRight={
          <Switch
            checked={filterEnabled ?? false}
            onCheckedChange={setFilterEnabledLocal}
            disabled={filterEnabledQ.isLoading}
            aria-label="Enable ADC filter"
          />
        }
        footer={
          <>
            {filterDirty && (
              <span className="mr-auto text-xs text-warning">Unsaved changes</span>
            )}
            <Button
              size="sm"
              disabled={!filterDirty || savingFilter}
              onClick={() => void saveFilter()}
            >
              {savingFilter ? "Writing…" : "Write to device"}
            </Button>
          </>
        }
      >
        <div className="grid gap-5 lg:grid-cols-3">
          <SliderField
            label="Noise band"
            description="Movement below this is treated as noise."
            min={1} max={255} step={1}
            value={noise}
            onCommit={setNoise}
            disabled={!filterEnabled}
          />
          <SliderField
            label="Alpha min denominator"
            description="Smoothing while the key moves fast."
            min={1} max={255} step={1}
            value={alphaMin}
            onCommit={setAlphaMin}
            disabled={!filterEnabled}
          />
          <SliderField
            label="Alpha max denominator"
            description="Smoothing while the key is nearly still."
            min={1} max={255} step={1}
            value={alphaMax}
            onCommit={setAlphaMax}
            disabled={!filterEnabled}
          />
        </div>
      </SectionCard>

      <SectionCard
        title="Waveform capture"
        description="Record raw and filtered samples for a single key at full scan rate."
        icon={<IconWaveSquare />}
      >
        <div className="flex flex-col gap-4">
          <div className="flex flex-wrap items-end gap-3">
            <label className="grid gap-1">
              <span className="text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
                Key index
              </span>
              <Input
                type="number"
                min={0}
                max={KEY_COUNT - 1}
                value={captureKey}
                onChange={(e) =>
                  setCaptureKey(Math.max(0, Math.min(KEY_COUNT - 1, +e.target.value)))
                }
                className="h-8 w-24 font-mono text-xs"
              />
            </label>
            <label className="grid gap-1">
              <span className="text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
                Duration (ms)
              </span>
              <Input
                type="number"
                min={1}
                max={10000}
                value={captureDuration}
                onChange={(e) => setCaptureDuration(+e.target.value)}
                className="h-8 w-28 font-mono text-xs"
              />
            </label>
            <span className="pb-1.5 text-xs text-muted-foreground">
              {KEY_LABELS[captureKey] ?? "—"}
            </span>
            <Button
              size="sm"
              className="ml-auto"
              disabled={capturing}
              onClick={() => void startCapture()}
            >
              <IconPlayerPlay className="size-3.5" />
              {capturing ? "Capturing…" : "Start capture"}
            </Button>
          </div>

          {captureStatus && (
            <div className="flex flex-col gap-2 rounded-lg border bg-surface-sunken px-3 py-2.5">
              <div className="flex flex-wrap gap-x-5 gap-y-1 font-mono text-xs tabular-nums">
                <span className="text-muted-foreground">
                  state{" "}
                  <span className={captureStatus.active ? "text-warning" : "text-success"}>
                    {captureStatus.active ? "running" : "idle"}
                  </span>
                </span>
                <span className="text-muted-foreground">
                  key <span className="text-foreground">{captureStatus.key_index}</span>
                </span>
                <span className="text-muted-foreground">
                  samples <span className="text-foreground">{captureStatus.sample_count}</span>
                </span>
                <span className="text-muted-foreground">
                  overflow{" "}
                  <span className={captureStatus.overflow_count > 0 ? "text-destructive" : "text-foreground"}>
                    {captureStatus.overflow_count}
                  </span>
                </span>
              </div>
              {captureStatus.active && <Progress value={50} className="h-1" />}
            </div>
          )}

          {captureData && captureData.raw.length > 0 ? (
            <div className="rounded-lg border bg-surface-sunken p-3">
              <div className="mb-2 flex gap-4">
                <LegendSwatch color={SERIES_COLORS[0]}>
                  Raw ({captureData.raw.length})
                </LegendSwatch>
                <LegendSwatch color={SERIES_COLORS[2]}>
                  Filtered ({captureData.filtered.length})
                </LegendSwatch>
              </div>
              <svg
                viewBox={`0 0 ${captureData.raw.length} ${ADC_FULL_SCALE}`}
                className="h-36 w-full rounded"
                preserveAspectRatio="none"
              >
                {[
                  { data: captureData.raw, color: SERIES_COLORS[0] },
                  { data: captureData.filtered, color: SERIES_COLORS[2] },
                ].map(({ data: arr, color }) => (
                  <polyline
                    key={color}
                    points={arr.map((v, i) => `${i},${ADC_FULL_SCALE - v}`).join(" ")}
                    fill="none"
                    stroke={color}
                    strokeWidth={1.5}
                    strokeLinejoin="round"
                    vectorEffect="non-scaling-stroke"
                  />
                ))}
              </svg>
            </div>
          ) : (
            !capturing && (
              <EmptyState
                size="sm"
                icon={<IconWaveSquare />}
                title="No capture yet"
                description="Pick a key and press Start capture, then move that key while it records."
              />
            )
          )}
        </div>
      </SectionCard>

      <SectionCard
        title="SOCD pairs"
        description="Keys resolving simultaneous opposing directions against each other."
        icon={<IconArrowsExchange />}
        headerRight={
          <Button
            size="sm"
            variant="outline"
            disabled={allSettingsQ.isFetching}
            onClick={() => void allSettingsQ.refetch()}
          >
            <IconRefresh className={cn("size-3.5", allSettingsQ.isFetching && "animate-spin")} />
            Reload
          </Button>
        }
      >
        {allSettingsQ.isLoading ? (
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            {[0, 1].map((i) => <Skeleton key={i} className="h-10 rounded-lg" />)}
          </div>
        ) : socdPairs.length === 0 ? (
          <EmptyState
            size="sm"
            icon={<IconArrowsExchange />}
            title="No SOCD pairs configured"
            description="Pair opposing keys on the Advanced Keys page to see them listed here."
          />
        ) : (
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            {socdPairs.map(({ a, b, resolution }) => (
              <div
                key={`${a}-${b}`}
                className="flex items-center gap-2 rounded-lg border bg-card px-3 py-2"
              >
                <Badge variant="outline" className="font-mono text-[0.65rem]">
                  {KEY_LABELS[a]} #{a}
                </Badge>
                <IconArrowsExchange className="size-4 shrink-0 text-muted-foreground" />
                <Badge variant="outline" className="font-mono text-[0.65rem]">
                  {KEY_LABELS[b]} #{b}
                </Badge>
                <Badge variant="secondary" className="ml-auto text-[0.65rem]">
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

// ── Measure tab: record ADC against known travel, for curve fitting ─────────

function MeasureTab({ connected, active }: { connected: boolean; active: boolean }) {
  const [filename, setFilename] = useState("measurements.csv");
  const [step, setStep] = useState("0.1");
  const [keyToTrack, setKeyToTrack] = useState("0");
  const [useMedian, setUseMedian] = useState(false);

  const [tracking, setTracking] = useState(false);
  const [currentDistanceStr, setCurrentDistanceStr] = useState("0");
  const [measurements, setMeasurements] = useState<{ distance: number; rawAdc: number }[]>([]);
  const [recentSamples, setRecentSamples] = useState<number[]>([]);

  const rawQ = useQuery({
    queryKey: ["diagnostics", "rawAdc"],
    queryFn: () => kbheDevice.getAllRawAdcValues(),
    enabled: connected && tracking && active,
    refetchInterval: connected && tracking && active ? 80 : false,
  });

  const kIdx = parseInt(keyToTrack, 10);
  const validKey = Number.isFinite(kIdx) && kIdx >= 0 && kIdx < KEY_COUNT;
  const currentRawAdc = validKey && rawQ.data ? rawQ.data[kIdx] ?? 0 : 0;

  useEffect(() => {
    if (!tracking || !rawQ.data || !validKey) return;
    const sample = rawQ.data[kIdx] ?? 0;
    setRecentSamples((prev) => {
      const next = [...prev, sample];
      return next.length > 8 ? next.slice(next.length - 8) : next;
    });
  }, [rawQ.data, tracking, kIdx, validKey]);

  const displayAdc = useMedian && recentSamples.length > 0
    ? [...recentSamples].sort((a, b) => a - b)[Math.floor(recentSamples.length / 2)]
    : currentRawAdc;

  const handleStart = () => {
    setTracking(true);
    setCurrentDistanceStr("0");
    setMeasurements([]);
    setRecentSamples([]);
  };

  const handleSaveMeasurement = () => {
    const dist = parseFloat(currentDistanceStr);
    const inc = parseFloat(step);
    if (!Number.isFinite(dist)) return;
    setMeasurements((prev) => [...prev, { distance: dist, rawAdc: displayAdc }]);
    setCurrentDistanceStr((dist + (Number.isFinite(inc) ? inc : 0)).toFixed(3));
  };

  const handleDownloadCsv = () => {
    const csvHeader = "distance_mm,raw_adc\n";
    const csvContent = measurements.map((m) => `${m.distance},${m.rawAdc}`).join("\n");
    const blob = new Blob([csvHeader + csvContent], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename.endsWith(".csv") ? filename : `${filename}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  };

  if (!connected) return <DisconnectedBanner />;

  return (
    <div className="flex flex-col gap-5">
      <SectionCard
        tone="muted"
        title="How this works"
        description="Build a distance-to-ADC table for one key, then fit a curve from it."
        icon={<IconRuler />}
      >
        <ol className="flex flex-col gap-1.5 text-xs leading-relaxed text-muted-foreground">
          <li>
            <span className="font-medium text-foreground">1.</span> Pick the key you can press
            to a measured depth, and the step you will move it by.
          </li>
          <li>
            <span className="font-medium text-foreground">2.</span> Hold the key at each depth
            and press <span className="font-medium text-foreground">Record point</span>. The
            distance field advances by one step automatically.
          </li>
          <li>
            <span className="font-medium text-foreground">3.</span> Export the table as CSV once
            the curve covers the travel range.
          </li>
        </ol>
      </SectionCard>

      {!tracking ? (
        <SectionCard
          title="Session setup"
          description="Nothing is written to the keyboard — this only reads sensor values."
          icon={<IconCrosshair />}
          footer={
            <Button size="sm" disabled={!validKey} onClick={handleStart}>
              <IconPlayerPlay className="size-3.5" />
              Start measuring
            </Button>
          }
        >
          <FormRows>
            <FormRow
              label="Key to track"
              description={
                validKey
                  ? `Recording sensor values from ${KEY_LABELS[kIdx]}.`
                  : `Enter an index between 0 and ${KEY_COUNT - 1}.`
              }
            >
              <Input
                type="number"
                min={0}
                max={KEY_COUNT - 1}
                className="h-8 w-24 font-mono text-xs"
                value={keyToTrack}
                onChange={(e) => setKeyToTrack(e.target.value)}
                aria-invalid={!validKey}
              />
            </FormRow>
            <FormRow
              label="Distance step"
              description="How far you move the key between two recorded points."
            >
              <div className="flex items-center gap-1.5">
                <Input
                  type="number"
                  step="0.01"
                  className="h-8 w-24 font-mono text-xs"
                  value={step}
                  onChange={(e) => setStep(e.target.value)}
                />
                <span className="text-xs text-muted-foreground">mm</span>
              </div>
            </FormRow>
            <FormRow
              label="Median filter"
              description="Record the median of the last 8 samples instead of the newest one."
            >
              <Switch checked={useMedian} onCheckedChange={setUseMedian} />
            </FormRow>
            <FormRow
              label="Export filename"
              description="Used when you download the table as CSV."
            >
              <Input
                className="h-8 w-56 font-mono text-xs"
                autoComplete="off"
                value={filename}
                onChange={(e) => setFilename(e.target.value)}
              />
            </FormRow>
          </FormRows>
        </SectionCard>
      ) : (
        <>
          <SectionCard
            title={`Recording ${KEY_LABELS[kIdx] ?? keyToTrack}`}
            description={
              useMedian
                ? "Live reading is the median of the last 8 samples."
                : "Live reading is the most recent sample."
            }
            icon={<IconCrosshair />}
            headerRight={
              <>
                <ToolbarStat label="Points" value={String(measurements.length)} tone="active" />
                <span className="rounded-md border bg-muted/40 px-2.5 py-1 font-mono text-sm tabular-nums">
                  {displayAdc}
                </span>
              </>
            }
            footer={
              <>
                <Button variant="outline" size="sm" onClick={() => setTracking(false)}>
                  Stop
                </Button>
                <Button
                  size="sm"
                  onClick={handleDownloadCsv}
                  disabled={measurements.length === 0}
                >
                  <IconFileExport className="size-3.5" />
                  Download CSV
                </Button>
              </>
            }
          >
            <div className="flex flex-wrap items-end gap-3">
              <label className="grid flex-1 gap-1">
                <span className="text-[0.68rem] font-medium uppercase tracking-[0.05em] text-muted-foreground">
                  Current depth (mm)
                </span>
                <Input
                  type="number"
                  step="0.01"
                  className="h-9 font-mono"
                  value={currentDistanceStr}
                  onChange={(e) => setCurrentDistanceStr(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === "Enter") handleSaveMeasurement();
                  }}
                />
              </label>
              <Button className="h-9" onClick={handleSaveMeasurement}>
                <IconDownload className="size-4" />
                Record point
              </Button>
            </div>
          </SectionCard>

          <SectionCard
            title="Captured curve"
            description="Raw ADC plotted against the depth you entered."
            icon={<IconChartLine />}
            headerRight={
              measurements.length > 0 ? (
                <Button
                  variant="ghost"
                  size="sm"
                  onClick={() => setMeasurements([])}
                >
                  <IconEraser className="size-3.5" />
                  Discard points
                </Button>
              ) : undefined
            }
          >
            {measurements.length === 0 ? (
              <EmptyState
                icon={<IconChartLine />}
                title="No points recorded"
                description="Hold the key at a known depth and press Record point to add the first sample."
              />
            ) : (
              <XYChart
                measurements={measurements}
                xMin={Math.min(0, ...measurements.map((m) => m.distance))}
                xMax={Math.max(4, ...measurements.map((m) => m.distance))}
                yMin={Math.min(...measurements.map((m) => m.rawAdc)) * 0.95}
                yMax={Math.max(...measurements.map((m) => m.rawAdc)) * 1.05}
              />
            )}
          </SectionCard>
        </>
      )}
    </div>
  );
}

// ── Page ─────────────────────────────────────────────────────────────────────

const DIAGNOSTIC_TABS = [
  {
    value: "live",
    label: "Live",
    icon: IconKeyboard,
    description: "Travel heatmap and per-key inspector",
    width: "max-w-6xl",
  },
  {
    value: "sensors",
    label: "Sensors",
    icon: IconWaveSquare,
    description: "Every ADC reading on the board",
    width: "max-w-6xl",
  },
  {
    value: "scope",
    label: "Scope",
    icon: IconChartLine,
    description: "Rolling signal capture for chosen keys",
    width: "max-w-6xl",
  },
  {
    value: "system",
    label: "System",
    icon: IconCpu,
    description: "MCU counters, configuration and low-level tools",
    width: "max-w-6xl",
  },
  {
    value: "measure",
    label: "Measure",
    icon: IconRuler,
    description: "Record travel against ADC for curve fitting",
    width: "max-w-4xl",
  },
] as const;

export default function Diagnostics() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const [activeTab, setActiveTab] = useState<string>("live");

  const current =
    DIAGNOSTIC_TABS.find((tab) => tab.value === activeTab) ?? DIAGNOSTIC_TABS[0];

  return (
    <div className="flex h-full min-h-0 flex-col overflow-hidden">
      <VitalsBar connected={connected} />

      <Tabs
        value={activeTab}
        onValueChange={setActiveTab}
        className="flex h-full min-h-0 flex-col"
      >
        <div className="flex shrink-0 items-center justify-between gap-4 border-b bg-background/60 px-5 py-2.5">
          <TabsList className="h-8">
            {DIAGNOSTIC_TABS.map((tab) => (
              <TabsTrigger key={tab.value} value={tab.value} className="gap-1.5 text-xs">
                <tab.icon className="size-3.5" />
                {tab.label}
              </TabsTrigger>
            ))}
          </TabsList>
          <div className="flex shrink-0 items-center gap-2.5">
            <span className="hidden text-xs text-muted-foreground xl:inline">
              {current.description}
            </span>
            <Badge
              variant="outline"
              className="border-warning/40 bg-warning/10 text-warning"
              title="Diagnostics are developer tools; values come straight from the firmware."
            >
              Developer
            </Badge>
          </div>
        </div>

        {DIAGNOSTIC_TABS.map((tab) => (
          // Base UI keeps the outgoing panel mounted until its exit transition
          // finishes. These panels declare no exit transition, so it never
          // unmounts and both panels would render stacked — hide it on the way out.
          <TabsContent
            key={tab.value}
            value={tab.value}
            className="mt-0 min-h-0 flex-1 [&[data-ending-style]]:hidden"
          >
            <ScrollArea className="h-full">
              <div className={cn("mx-auto flex w-full flex-col gap-5 px-5 py-5", tab.width)}>
                {tab.value === "live" && (
                  <LiveTab connected={connected} active={activeTab === "live"} />
                )}
                {tab.value === "sensors" && (
                  <SensorsTab connected={connected} active={activeTab === "sensors"} />
                )}
                {tab.value === "scope" && (
                  <ScopeTab connected={connected} active={activeTab === "scope"} />
                )}
                {tab.value === "system" && (
                  <SystemTab connected={connected} active={activeTab === "system"} />
                )}
                {tab.value === "measure" && (
                  <MeasureTab connected={connected} active={activeTab === "measure"} />
                )}
              </div>
            </ScrollArea>
          </TabsContent>
        ))}
      </Tabs>
    </div>
  );
}
