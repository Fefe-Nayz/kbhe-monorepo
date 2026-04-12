import { useState, useCallback, useMemo, useEffect, useRef } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { KeyTester } from "@/components/key-tester";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { SectionCard } from "@/components/shared/SectionCard";
import { useAutosave } from "@/components/AutosaveStatus";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Progress } from "@/components/ui/progress";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { CommitSlider } from "@/components/ui/commit-slider";
import { Switch } from "@/components/ui/switch";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { useTheme } from "@/components/theme-provider";
import { previewKeys } from "@/constants/defaultLayout";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { useDeviceSession } from "@/lib/kbhe/session";
import {
  kbheDevice,
  type CalibrationSettings,
  type GuidedCalibrationStatus,
  type KeyStatesSnapshot,
} from "@/lib/kbhe/device";
import { KEY_COUNT } from "@/lib/kbhe/protocol";
import { usePageVisible } from "@/hooks/use-page-visible";
import {
  IconRefresh,
  IconPlayerPlay,
  IconPlayerStop,
} from "@tabler/icons-react";

const MAX_TRAVEL_MM = 4.0;
const MAX_ADC_VALUE = 255;
const KEY_STATES_POLL_INTERVAL_MS = 20;
const KEYBOARD_PREVIEW_FRAME_MS = 33;

type CalibrationPreviewMode = "live" | "heatmap";
type CalibrationValueMode = "mm" | "adc";

function heatmapColor(t: number, isDark: boolean): string {
  const clamped = Math.min(1, Math.max(0, t));
  const hue = 120 - clamped * 120;
  const saturation = isDark ? 72 : 68;
  const lightness = isDark
    ? 26 + clamped * 36
    : 88 - clamped * 42;
  return `hsl(${hue} ${saturation}% ${lightness}%)`;
}

function calibrationDeltaColor(delta: number, maxDelta: number, isDark: boolean): string {
  const magnitude = Math.min(1, Math.max(0, Math.abs(delta) / Math.max(1, maxDelta)));
  const hue = delta >= 0 ? 212 : 152;
  const saturation = isDark ? 70 : 66;
  const lightness = isDark
    ? 24 + magnitude * 34
    : 90 - magnitude * 40;
  return `hsl(${hue} ${saturation}% ${lightness}%)`;
}

type GuidedState = "idle" | "running" | "success" | "error";

export default function Calibration() {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const { resolvedTheme } = useTheme();
  const { status }   = useDeviceSession();
  const connected    = status === "connected";
  const visible      = usePageVisible();
  const qc           = useQueryClient();
  const { markSaving, markSaved, markError } = useAutosave();

  const [activeTab, setActiveTab] = useState("status");
  const [previewMode, setPreviewMode] = useState<CalibrationPreviewMode>("live");
  const [valueMode, setValueMode] = useState<CalibrationValueMode>("mm");
  const [guidedState, setGuidedState] = useState<GuidedState>("idle");
  const [guidedProgress, setGuidedProgress] = useState(0);
  const [guidedStatus, setGuidedStatus] = useState<GuidedCalibrationStatus | null>(null);
  const [guidedBlinkOn, setGuidedBlinkOn] = useState(true);
  const [guidedSuccessBlinkActive, setGuidedSuccessBlinkActive] = useState(false);
  const [liveKeyStates, setLiveKeyStates] = useState<KeyStatesSnapshot | null>(null);
  const [previewKeyStates, setPreviewKeyStates] = useState<KeyStatesSnapshot | null>(null);
  const prevStatesRef = useRef<Uint8Array | null>(null);
  const prevDistanceTenthsRef = useRef<Int16Array | null>(null);
  const previewPendingRef = useRef<KeyStatesSnapshot | null>(null);
  const previewFlushTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const previewLastPushRef = useRef(0);
  const guidedSuccessBlinkTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastGuidedPhaseRef = useRef<number | null>(null);

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10) : null;
  const isDarkTheme = resolvedTheme === "dark";
  const focusedKeyLabel = useMemo(() => {
    if (keyIndex == null || keyIndex < 0 || keyIndex >= KEY_COUNT) {
      return "—";
    }
    return previewKeys[keyIndex]?.baseLabel?.trim() || `K${keyIndex + 1}`;
  }, [keyIndex]);

  const calibrationQ = useQuery({
    queryKey: ["calibration", "all"],
    queryFn: () => kbheDevice.getCalibration(),
    enabled: connected,
    staleTime: 30_000,
  });

  const polling = connected && visible && activeTab === "status";

  const flushPreviewSnapshot = useCallback(() => {
    previewFlushTimerRef.current = null;
    const pending = previewPendingRef.current;
    if (!pending) {
      return;
    }
    previewPendingRef.current = null;
    previewLastPushRef.current = performance.now();
    setPreviewKeyStates(pending);
  }, []);

  const enqueuePreviewSnapshot = useCallback((snapshot: KeyStatesSnapshot) => {
    previewPendingRef.current = snapshot;

    const now = performance.now();
    const elapsed = now - previewLastPushRef.current;
    const waitMs = Math.max(0, KEYBOARD_PREVIEW_FRAME_MS - elapsed);

    if (waitMs <= 0 && !previewFlushTimerRef.current) {
      flushPreviewSnapshot();
      return;
    }

    if (!previewFlushTimerRef.current) {
      previewFlushTimerRef.current = setTimeout(flushPreviewSnapshot, waitMs);
    }
  }, [flushPreviewSnapshot]);

  useEffect(() => {
    if (!polling) {
      setLiveKeyStates(null);
      setPreviewKeyStates(null);
      prevStatesRef.current = null;
      prevDistanceTenthsRef.current = null;
      previewPendingRef.current = null;
      previewLastPushRef.current = 0;
      if (previewFlushTimerRef.current) {
        clearTimeout(previewFlushTimerRef.current);
        previewFlushTimerRef.current = null;
      }
      return;
    }

    let disposed = false;
    let timer: ReturnType<typeof setTimeout> | null = null;
    let inFlight = false;

    const tick = async () => {
      if (disposed || inFlight) {
        return;
      }

      inFlight = true;
      try {
        const snapshot = await kbheDevice.getKeyStates();
        if (disposed || !snapshot) {
          return;
        }

        const prevStates = prevStatesRef.current;
        const prevDistances = prevDistanceTenthsRef.current;
        const nextStates = snapshot.states;
        const nextDistances = snapshot.distances_mm;

        let statesChanged =
          !prevStates ||
          prevStates.length !== nextStates.length;
        let distancesChanged =
          !prevDistances ||
          prevDistances.length !== nextDistances.length;

        const stateBuffer = new Uint8Array(nextStates.length);
        const distanceBuffer = new Int16Array(nextDistances.length);

        for (let i = 0; i < nextStates.length; i++) {
          const state = nextStates[i] ? 1 : 0;
          const distTenths = Math.round((nextDistances[i] ?? 0) * 10);

          stateBuffer[i] = state;
          distanceBuffer[i] = distTenths;

          if (!statesChanged && prevStates && prevStates[i] !== state) {
            statesChanged = true;
          }
          if (!distancesChanged && prevDistances && prevDistances[i] !== distTenths) {
            distancesChanged = true;
          }
        }

        if (statesChanged || distancesChanged) {
          prevStatesRef.current = stateBuffer;
          prevDistanceTenthsRef.current = distanceBuffer;
          enqueuePreviewSnapshot(snapshot);
        }

        if (statesChanged) {
          setLiveKeyStates(snapshot);
        }
      } catch {
        // ignore transient read errors while polling
      } finally {
        inFlight = false;
        if (!disposed) {
          timer = setTimeout(() => void tick(), KEY_STATES_POLL_INTERVAL_MS);
        }
      }
    };

    void tick();

    return () => {
      disposed = true;
      if (timer) {
        clearTimeout(timer);
      }
      if (previewFlushTimerRef.current) {
        clearTimeout(previewFlushTimerRef.current);
        previewFlushTimerRef.current = null;
      }
    };
  }, [enqueuePreviewSnapshot, polling]);

  const autoCalibrateMutation = useMutation({
    mutationFn: async (idx: number) => {
      markSaving();
      const updated = await kbheDevice.autoCalibrate(idx);
      if (!updated) {
        throw new Error("Auto calibration failed");
      }
      return updated;
    },
    onSuccess: (updated) => {
      qc.setQueryData<CalibrationSettings>(["calibration", "all"], updated);
      markSaved();
    },
    onError: markError,
  });

  const applyCalibrationForKey = useCallback(async (
    field: "zero" | "max",
    value: number,
    notify: boolean,
  ) => {
    if (keyIndex == null || !calibrationQ.data) {
      return;
    }

    const zeros = Array.from(calibrationQ.data.key_zero_values);
    const maxs = Array.from(calibrationQ.data.key_max_values);

    if (field === "zero") {
      zeros[keyIndex] = value;
    } else {
      maxs[keyIndex] = value;
    }

    try {
      if (notify) {
        markSaving();
      }

      const ok = await kbheDevice.setCalibration(
        calibrationQ.data.lut_zero_value,
        zeros,
        maxs,
      );

      if (!ok) {
        throw new Error("Calibration update rejected by device");
      }

      qc.setQueryData<CalibrationSettings>(["calibration", "all"], {
        ...calibrationQ.data,
        key_zero_values: zeros,
        key_max_values: maxs,
      });

      if (notify) {
        markSaved();
      }
    } catch (error) {
      if (notify) {
        markError(error);
      }
    }
  }, [calibrationQ.data, keyIndex, markError, markSaved, markSaving, qc]);

  const liveZeroUpdate = useThrottledCall<number>(async (value) => {
    await applyCalibrationForKey("zero", value, false);
  });

  const liveMaxUpdate = useThrottledCall<number>(async (value) => {
    await applyCalibrationForKey("max", value, false);
  });

  const guidedStart = useCallback(async () => {
    if (!connected) return;
    setGuidedState("running");
    setGuidedProgress(0);
    setGuidedSuccessBlinkActive(false);
    try {
      const status = await kbheDevice.guidedCalibrationStart();
      if (!status) {
        throw new Error("Failed to start guided calibration");
      }
      setGuidedStatus(status);
      setGuidedProgress(status.progress_percent ?? 0);
      lastGuidedPhaseRef.current = status.phase;
    } catch {
      setGuidedState("error");
    }
  }, [connected]);

  const guidedAbort = useCallback(async () => {
    try {
      const status = await kbheDevice.guidedCalibrationAbort();
      setGuidedStatus(status);
      if (status) {
        setGuidedProgress(status.progress_percent ?? 0);
        lastGuidedPhaseRef.current = status.phase;
      }
    } catch { /* ignore */ }
    setGuidedState("idle");
    setGuidedProgress(0);
    setGuidedSuccessBlinkActive(false);
  }, []);

  useEffect(() => {
    return () => {
      if (guidedSuccessBlinkTimerRef.current) {
        clearTimeout(guidedSuccessBlinkTimerRef.current);
      }
    };
  }, []);

  const guidedStatusPolling = connected && visible && (activeTab === "guided" || guidedState === "running");

  useEffect(() => {
    if (!guidedStatusPolling) {
      return;
    }

    let disposed = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const tick = async () => {
      try {
        const status = await kbheDevice.guidedCalibrationStatus();
        if (disposed || !status) {
          return;
        }

        setGuidedStatus(status);
        setGuidedProgress(status.progress_percent ?? 0);

        const phaseChanged = lastGuidedPhaseRef.current !== status.phase;
        lastGuidedPhaseRef.current = status.phase;

        if (status.active) {
          setGuidedState("running");
        } else if (status.phase === 4) {
          setGuidedState("success");
          setGuidedProgress(100);
          if (phaseChanged) {
            setGuidedSuccessBlinkActive(true);
            if (guidedSuccessBlinkTimerRef.current) {
              clearTimeout(guidedSuccessBlinkTimerRef.current);
            }
            guidedSuccessBlinkTimerRef.current = setTimeout(() => {
              setGuidedSuccessBlinkActive(false);
            }, 2200);
            void qc.invalidateQueries({ queryKey: ["calibration"] });
          }
        } else if (status.phase === 6) {
          setGuidedState("error");
          setGuidedSuccessBlinkActive(false);
        } else {
          setGuidedState("idle");
          setGuidedSuccessBlinkActive(false);
        }
      } catch {
        // Ignore transient guided status polling errors.
      } finally {
        if (!disposed) {
          timer = setTimeout(() => void tick(), 200);
        }
      }
    };

    void tick();

    return () => {
      disposed = true;
      if (timer) {
        clearTimeout(timer);
      }
    };
  }, [guidedState, guidedStatusPolling, qc]);

  const guidedNeedsBlink =
    activeTab === "guided" &&
    (guidedSuccessBlinkActive || Boolean(guidedStatus?.active && guidedStatus.phase === 1));

  useEffect(() => {
    if (!guidedNeedsBlink) {
      setGuidedBlinkOn(true);
      return;
    }

    setGuidedBlinkOn(true);
    const timer = setInterval(() => {
      setGuidedBlinkOn((value) => !value);
    }, 220);

    return () => {
      clearInterval(timer);
    };
  }, [guidedNeedsBlink]);

  const previewDistancesMm = previewKeyStates?.distances_mm;
  const previewAdcValues = previewKeyStates?.distances;
  const isStatusTab = activeTab === "status";
  const isGuidedTab = activeTab === "guided";
  const guidedTargetLabel =
    guidedStatus && guidedStatus.current_key >= 0 && guidedStatus.current_key < KEY_COUNT
      ? (previewKeys[guidedStatus.current_key]?.baseLabel?.trim() || `K${guidedStatus.current_key + 1}`)
      : null;

  const calibrationHeatmapData = useMemo(() => {
    const calibration = calibrationQ.data;
    if (!calibration) return null;

    const lutZero = Math.trunc(calibration.lut_zero_value ?? 0);
    const zeros = calibration.key_zero_values ?? [];
    const deltas = Array.from({ length: KEY_COUNT }, (_, i) => {
      const zero = Math.trunc(zeros[i] ?? lutZero);
      return zero - lutZero;
    });
    const maxDelta = Math.max(1, ...deltas.map((delta) => Math.abs(delta)));

    return { deltas, maxDelta };
  }, [calibrationQ.data]);

  const statusKeyColorMap = useMemo(() => {
    if (previewMode === "heatmap") {
      if (!calibrationHeatmapData) return undefined;
      const map: Record<string, string> = {};
      for (let i = 0; i < KEY_COUNT; i++) {
        const delta = calibrationHeatmapData.deltas[i] ?? 0;
        map[`key-${i}`] = calibrationDeltaColor(delta, calibrationHeatmapData.maxDelta, isDarkTheme);
      }
      return map;
    }

    if (!previewDistancesMm && !previewAdcValues) return undefined;
    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const value = valueMode === "mm"
        ? (previewDistancesMm?.[i] ?? 0)
        : (previewAdcValues?.[i] ?? 0);
      const normalized = valueMode === "mm"
        ? value / MAX_TRAVEL_MM
        : value / MAX_ADC_VALUE;
      map[`key-${i}`] = heatmapColor(normalized, isDarkTheme);
    }
    return map;
  }, [calibrationHeatmapData, isDarkTheme, previewAdcValues, previewDistancesMm, previewMode, valueMode]);

  const guidedKeyColorMap = useMemo(() => {
    if (!isGuidedTab) {
      return undefined;
    }

    const brightRed = isDarkTheme ? "hsl(0 88% 56%)" : "hsl(0 84% 52%)";
    const brightGreen = isDarkTheme ? "hsl(142 80% 50%)" : "hsl(142 74% 45%)";

    if (guidedSuccessBlinkActive) {
      if (!guidedBlinkOn) {
        return undefined;
      }
      const map: Record<string, string> = {};
      for (let i = 0; i < KEY_COUNT; i++) {
        map[`key-${i}`] = brightGreen;
      }
      return map;
    }

    if (!guidedStatus?.active) {
      return undefined;
    }

    if (guidedStatus.phase === 1) {
      if (!guidedBlinkOn) {
        return undefined;
      }
      const map: Record<string, string> = {};
      for (let i = 0; i < KEY_COUNT; i++) {
        map[`key-${i}`] = brightRed;
      }
      return map;
    }

    if (guidedStatus.phase === 2 || guidedStatus.phase === 3) {
      const index = guidedStatus.current_key;
      if (index < 0 || index >= KEY_COUNT) {
        return undefined;
      }
      return { [`key-${index}`]: brightRed };
    }

    return undefined;
  }, [guidedBlinkOn, guidedStatus, guidedSuccessBlinkActive, isDarkTheme, isGuidedTab]);

  const keyLegendSlotsMap = useMemo(() => {
    const map: Record<string, Array<string | undefined>> = {};
    const slots = Array.from({ length: 12 }, (_, index) => (index === 0 ? undefined : ""));
    for (const key of previewKeys) {
      map[key.id] = slots;
    }
    return map;
  }, []);

  const keyValueLegendMap = useMemo(() => {
    const map: Record<string, string> = {};

    if (previewMode === "heatmap") {
      if (!calibrationHeatmapData) return undefined;
      for (let i = 0; i < KEY_COUNT; i++) {
        const delta = calibrationHeatmapData.deltas[i] ?? 0;
        map[`key-${i}`] = delta > 0 ? `+${delta}` : `${delta}`;
      }
      return map;
    }

    if (!previewDistancesMm && !previewAdcValues) return undefined;
    for (let i = 0; i < KEY_COUNT; i++) {
      if (valueMode === "mm") {
        const distMm = previewDistancesMm?.[i] ?? 0;
        map[`key-${i}`] = distMm.toFixed(1);
      } else {
        const adc = Math.round(previewAdcValues?.[i] ?? 0);
        map[`key-${i}`] = String(adc);
      }
    }

    return map;
  }, [calibrationHeatmapData, previewAdcValues, previewDistancesMm, previewMode, valueMode]);

  const statusLegendClassName = useMemo(
    () =>
      previewMode === "heatmap"
        ? isDarkTheme
          ? "text-[8px] font-mono font-semibold text-slate-100 drop-shadow-[0_0_2px_rgba(0,0,0,.7)]"
          : "text-[8px] font-mono font-semibold text-slate-900"
        : "text-[8px] font-mono font-semibold text-white drop-shadow-[0_0_2px_rgba(0,0,0,.8)]",
    [isDarkTheme, previewMode],
  );

  const previewKeyColorMap = isGuidedTab
    ? guidedKeyColorMap
    : isStatusTab
      ? statusKeyColorMap
      : undefined;

  const previewKeyLegendMap = isStatusTab && connected ? keyValueLegendMap : undefined;
  const previewKeyLegendClassName = isStatusTab ? statusLegendClassName : undefined;

  const formatCalibrationKeyValue = useCallback(
    ({ keyIndex, snapshot }: { keyIndex: number; snapshot: KeyStatesSnapshot | null; state: "pressed" | "released" }) => {
      if (valueMode === "adc") {
        const adcValue = snapshot?.distances?.[keyIndex];
        if (typeof adcValue !== "number" || Number.isNaN(adcValue)) {
          return "";
        }
        return `${Math.round(adcValue)}`;
      }

      const dist = snapshot?.distances_mm?.[keyIndex];
      if (typeof dist !== "number" || Number.isNaN(dist)) {
        return "";
      }
      return `${dist.toFixed(1)} mm`;
    },
    [valueMode],
  );

  const keyboardPreview = useMemo(() => (
    <BaseKeyboard
      mode="single"
      onButtonClick={() => {}}
      showLayerSelector={false}
      showRotary={false}
      showTooltips={false}
      keyColorMap={previewKeyColorMap}
      keyLegendMap={previewKeyLegendMap}
      keyLegendSlotsMap={keyLegendSlotsMap}
      keyLegendClassName={previewKeyLegendClassName}
    />
  ), [keyLegendSlotsMap, previewKeyColorMap, previewKeyLegendClassName, previewKeyLegendMap]);

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        {isStatusTab ? (
          <>
            {polling && (
              <Badge variant="secondary" className="gap-1 text-xs">
                Live
              </Badge>
            )}
            <Badge variant="outline" className="text-xs">
              {previewMode === "live" ? "Position View" : "Calibration Heatmap"}
            </Badge>
          </>
        ) : isGuidedTab ? (
          <>
            <Badge variant={guidedState === "running" ? "secondary" : "outline"} className="text-xs">
              {guidedState === "running" ? "Guided Running" : guidedState === "success" ? "Guided Complete" : "Guided"}
            </Badge>
            {guidedTargetLabel && (guidedStatus?.phase === 2 || guidedStatus?.phase === 3) && (
              <Badge variant="outline" className="text-xs">
                Press: {guidedTargetLabel}
              </Badge>
            )}
          </>
        ) : (
          <Badge variant="outline" className="text-xs">Manual</Badge>
        )}
      </div>
      <div className="flex items-center gap-2">
        {isStatusTab && (
          <div className="flex items-center gap-2 px-1">
            <Select
              value={valueMode}
              onValueChange={(value: string | null) => {
                if (value === "adc" || value === "mm") {
                  setValueMode(value);
                }
              }}
            >
              <SelectTrigger size="sm" className="h-8 w-40">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  <SelectItem value="mm">Distance (mm)</SelectItem>
                  <SelectItem value="adc">ADC</SelectItem>
                </SelectGroup>
              </SelectContent>
            </Select>

            <span className="text-xs text-muted-foreground">Position</span>
            <Switch
              checked={previewMode === "heatmap"}
              onCheckedChange={(checked) => setPreviewMode(checked ? "heatmap" : "live")}
            />
            <span className="text-xs text-muted-foreground">Heatmap ΔZero</span>
          </div>
        )}
        <Button variant="outline" size="sm" className="h-8"
          onClick={() => void qc.invalidateQueries({ queryKey: ["calibration"] })}>
          <IconRefresh className="size-4" />
          Reload
        </Button>
      </div>
    </>
  );

  return (
    <KeyboardEditor
      keyboard={keyboardPreview}
      menubar={menubar}
    >
      <Tabs value={activeTab} onValueChange={setActiveTab} className="w-full">
        <TabsList>
          <TabsTrigger value="status">Status</TabsTrigger>
          <TabsTrigger value="manual">Manual</TabsTrigger>
          <TabsTrigger value="guided">Guided</TabsTrigger>
        </TabsList>

        <TabsContent value="status" className="mt-4">
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <SectionCard title="Calibration Data">
              {calibrationQ.isLoading ? (
                <div className="space-y-2">{[0,1,2].map(i => <Skeleton key={i} className="h-6 w-full" />)}</div>
              ) : calibrationQ.data ? (
                <div className="flex flex-col gap-2 text-sm">
                  <div className="flex justify-between">
                    <span className="text-muted-foreground">Keys calibrated</span>
                    <span className="font-mono">{KEY_COUNT}</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-muted-foreground">LUT Reference</span>
                    <span className="font-mono">{calibrationQ.data.lut_zero_value}</span>
                  </div>
                  {keyIndex != null && calibrationQ.data.key_zero_values[keyIndex] !== undefined && (
                    <>
                      <div className="flex justify-between border-t pt-2">
                        <span className="text-muted-foreground">Key {focusedKeyLabel} Zero</span>
                        <span className="font-mono">{calibrationQ.data.key_zero_values[keyIndex]}</span>
                      </div>
                      <div className="flex justify-between">
                        <span className="text-muted-foreground">Key {focusedKeyLabel} Max</span>
                        <span className="font-mono">{calibrationQ.data.key_max_values[keyIndex]}</span>
                      </div>
                    </>
                  )}
                </div>
              ) : (
                <p className="text-sm text-muted-foreground">Connect device to view calibration.</p>
              )}
            </SectionCard>

            <SectionCard title="Key Tester">
              <KeyTester
                pressHeight="h-16"
                releaseHeight="h-20"
                snapshot={liveKeyStates}
                labelFormatter={formatCalibrationKeyValue}
                valueLabel={valueMode === "mm" ? "Distance" : "ADC"}
              />
            </SectionCard>
          </div>
        </TabsContent>

        <TabsContent value="manual" className="mt-4">
          <SectionCard title="Manual Calibration">
            <div className="flex flex-col gap-4">
              <div className="flex gap-2">
                <Button size="sm" disabled={!connected || keyIndex == null}
                  onClick={() => keyIndex != null && autoCalibrateMutation.mutate(keyIndex)}>
                  Auto Zero Key {focusedKeyLabel}
                </Button>
                <Button size="sm" variant="outline" disabled={!connected}
                  onClick={() => autoCalibrateMutation.mutate(0xff)}>
                  Auto Zero All
                </Button>
              </div>

              {keyIndex != null && calibrationQ.data && (
                <div className="flex flex-col gap-4 border-t pt-4">
                  <div className="grid gap-2">
                    <Label className="text-sm">Key {focusedKeyLabel} Zero</Label>
                    <CommitSlider
                      min={-32768} max={32767} step={1}
                      value={calibrationQ.data.key_zero_values[keyIndex] ?? 0}
                      onLiveChange={(v) => liveZeroUpdate(v)}
                      onCommit={(v) => {
                        void applyCalibrationForKey("zero", v, true);
                      }}
                      disabled={!connected}
                    />
                  </div>

                  <div className="grid gap-2">
                    <Label className="text-sm">Key {focusedKeyLabel} Max</Label>
                    <CommitSlider
                      min={-32768} max={32767} step={1}
                      value={calibrationQ.data.key_max_values[keyIndex] ?? 0}
                      onLiveChange={(v) => liveMaxUpdate(v)}
                      onCommit={(v) => {
                        void applyCalibrationForKey("max", v, true);
                      }}
                      disabled={!connected}
                    />
                  </div>
                </div>
              )}
            </div>
          </SectionCard>
        </TabsContent>

        <TabsContent value="guided" className="mt-4">
          <SectionCard title="Guided Calibration">
            <div className="flex flex-col gap-4">
              <p className="text-sm text-muted-foreground">
                Guided calibration will systematically calibrate all keys. Follow the on-screen instructions.
                The keyboard LEDs will indicate which key to press.
              </p>
              <div className="flex gap-2">
                {guidedState === "running" ? (
                  <Button variant="destructive" size="sm" onClick={() => void guidedAbort()}>
                    <IconPlayerStop className="size-4" />
                    Abort
                  </Button>
                ) : (
                  <Button size="sm" disabled={!connected} onClick={() => void guidedStart()}>
                    <IconPlayerPlay className="size-4" />
                    Start Guided Calibration
                  </Button>
                )}
              </div>

              {guidedState !== "idle" && (
                <div className="flex flex-col gap-2">
                  <Progress value={guidedProgress} className="h-2" />
                  <span className="text-xs text-muted-foreground">
                    {guidedState === "running" && `Calibrating... ${guidedProgress}%`}
                    {guidedState === "success" && "Calibration complete!"}
                    {guidedState === "error" && "Calibration failed."}
                  </span>
                </div>
              )}
            </div>
          </SectionCard>
        </TabsContent>
      </Tabs>
    </KeyboardEditor>
  );
}
