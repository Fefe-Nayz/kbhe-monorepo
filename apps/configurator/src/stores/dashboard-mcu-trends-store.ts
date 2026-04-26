import { create } from "zustand";
import { type McuMetrics } from "@/lib/kbhe/device";

const TREND_POINTS = 24;

export interface DashboardMcuTrends {
  temperature: number[];
  vref: number[];
  scanRate: number[];
  load: number[];
}

interface DashboardMcuTrendState {
  trends: DashboardMcuTrends;
  pushMetrics: (metrics: McuMetrics) => void;
}

function createEmptyTrends(): DashboardMcuTrends {
  return {
    temperature: [],
    vref: [],
    scanRate: [],
    load: [],
  };
}

function pushTrend(history: number[], value: number, maxPoints = TREND_POINTS): number[] {
  const next = [...history, value];
  if (next.length > maxPoints) {
    next.splice(0, next.length - maxPoints);
  }
  return next;
}

export const useDashboardMcuTrendStore = create<DashboardMcuTrendState>()((set) => ({
  trends: createEmptyTrends(),
  pushMetrics: (metrics) => {
    set((state) => ({
      trends: {
        temperature:
          metrics.temperature_valid && metrics.temperature_c != null
            ? pushTrend(state.trends.temperature, metrics.temperature_c)
            : state.trends.temperature,
        vref: pushTrend(state.trends.vref, metrics.vref_mv),
        scanRate: pushTrend(state.trends.scanRate, metrics.scan_rate_hz),
        load: pushTrend(state.trends.load, metrics.load_percent),
      },
    }));
  },
}));
