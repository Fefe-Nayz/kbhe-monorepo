import { useState, useCallback, useMemo, useEffect, useRef } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import BaseKeyboard from "@/components/baseKeyboard";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { SectionCard } from "@/components/shared/SectionCard";
import { useAutosave } from "@/components/AutosaveStatus";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Progress } from "@/components/ui/progress";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
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
const KEY_STATES_POLL_INTERVAL_MS = 20;
const KEYBOARD_PREVIEW_FRAME_MS = 33;
const MIN_CALIBRATION_VALUE = -32768;
const MAX_CALIBRATION_VALUE = 32767;

type GuidedState = "idle" | "running" | "success" | "error";
type CalibrationPreviewMode = "distance" | "offset";

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
  const hue = delta >= 0 ? 8 : 216;
  const saturation = isDark ? 74 : 68;
  const lightness = isDark
    ? 26 + magnitude * 30
    : 90 - magnitude * 42;
  return `hsl(${hue} ${saturation}% ${lightness}%)`;
}

export default function Calibration() {
  const selectedKeys = useKeyboardStore((s) => s.selectedKeys);
  const { resolvedTheme } = useTheme();
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const visible = usePageVisible();
  const qc = useQueryClient();
  const { markSaving, markSaved, markError } = useAutosave();

  const [guidedState, setGuidedState] = useState<GuidedState>("idle");
  const [guidedProgress, setGuidedProgress] = useState(0);
  const [guidedStatus, setGuidedStatus] = useState<GuidedCalibrationStatus | null>(null);
  const [guidedBlinkOn, setGuidedBlinkOn] = useState(true);
  const [guidedSuccessBlinkActive, setGuidedSuccessBlinkActive] = useState(false);
  const [previewMode, setPreviewMode] = useState<CalibrationPreviewMode>("distance");
  const [previewKeyStates, setPreviewKeyStates] = useState<KeyStatesSnapshot | null>(null);
  const [manualZeroInput, setManualZeroInput] = useState("");
  const [manualMaxInput, setManualMaxInput] = useState("");
  const [manualError, setManualError] = useState<string | null>(null);

  const prevStatesRef = useRef<Uint8Array | null>(null);
  const prevDistanceTenthsRef = useRef<Int16Array | null>(null);
  const previewPendingRef = useRef<KeyStatesSnapshot | null>(null);
  const previewFlushTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const previewLastPushRef = useRef(0);
  const guidedSuccessBlinkTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastGuidedPhaseRef = useRef<number | null>(null);

  const focusedKeyId = selectedKeys[0] ?? null;
  const keyIndex = focusedKeyId?.startsWith("key-")
    ? parseInt(focusedKeyId.replace("key-", ""), 10)
    : null;
  const isDarkTheme = resolvedTheme === "dark";

  const focusedKeyLabel = useMemo(() => {
    if (keyIndex == null || keyIndex < 0 || keyIndex >= KEY_COUNT) {
      return "-";
    }
    return previewKeys[keyIndex]?.baseLabel?.trim() || `K${keyIndex + 1}`;
  }, [keyIndex]);

  const calibrationQ = useQuery({
    queryKey: ["calibration", "all"],
    queryFn: () => kbheDevice.getCalibration(),
    enabled: connected,
    staleTime: 30_000,
  });

  const polling = connected && visible;

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

        let statesChanged = !prevStates || prevStates.length !== nextStates.length;
        let distancesChanged = !prevDistances || prevDistances.length !== nextDistances.length;

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
      } catch {
        // Ignore transient polling errors while the keyboard is connected.
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

  const commitCalibrationArrays = useCallback(async (
    zeros: number[],
    maxs: number[],
    notify: boolean,
  ) => {
    if (!calibrationQ.data) {
      return false;
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

      return true;
    } catch (error) {
      if (notify) {
        markError(error);
      }
      return false;
    }
  }, [calibrationQ.data, markError, markSaved, markSaving, qc]);

  const commitCalibrationForSelectedKey = useCallback(async (
    nextZero: number,
    nextMax: number,
    notify: boolean,
  ) => {
    if (keyIndex == null || !calibrationQ.data) {
      return false;
    }

    const zeros = Array.from(calibrationQ.data.key_zero_values);
    const maxs = Array.from(calibrationQ.data.key_max_values);
    zeros[keyIndex] = Math.trunc(nextZero);
    maxs[keyIndex] = Math.trunc(nextMax);

    return commitCalibrationArrays(zeros, maxs, notify);
  }, [calibrationQ.data, commitCalibrationArrays, keyIndex]);

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

  const guidedStart = useCallback(async () => {
    if (!connected) {
      return;
    }

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
    } catch {
      // Ignore transient abort errors.
    }

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

  useEffect(() => {
    if (!connected || !visible) {
      return;
    }

    let cancelled = false;

    void kbheDevice.guidedCalibrationStatus()
      .then((status) => {
        if (cancelled || !status) {
          return;
        }

        setGuidedStatus(status);
        setGuidedProgress(status.progress_percent ?? 0);
        lastGuidedPhaseRef.current = status.phase;
        if (status.active) {
          setGuidedState("running");
        }
      })
      .catch(() => {
        // Ignore status warm-up errors.
      });

    return () => {
      cancelled = true;
    };
  }, [connected, visible]);

  const guidedStatusPolling = connected && visible && (guidedState === "running" || Boolean(guidedStatus?.active));

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
  }, [guidedStatusPolling, qc]);

  const guidedNeedsBlink = guidedSuccessBlinkActive || Boolean(guidedStatus?.active && guidedStatus.phase === 1);

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

  useEffect(() => {
    if (keyIndex == null || !calibrationQ.data) {
      setManualZeroInput("");
      setManualMaxInput("");
      setManualError(null);
      return;
    }

    const nextZero = calibrationQ.data.key_zero_values[keyIndex] ?? 0;
    const nextMax = calibrationQ.data.key_max_values[keyIndex] ?? 0;
    setManualZeroInput(String(nextZero));
    setManualMaxInput(String(nextMax));
    setManualError(null);
  }, [calibrationQ.data, keyIndex]);

  const previewDistancesMm = previewKeyStates?.distances_mm;
  const guidedTargetLabel =
    guidedStatus && guidedStatus.current_key >= 0 && guidedStatus.current_key < KEY_COUNT
      ? (previewKeys[guidedStatus.current_key]?.baseLabel?.trim() || `K${guidedStatus.current_key + 1}`)
      : null;

  const calibrationHeatmapData = useMemo(() => {
    const calibration = calibrationQ.data;
    if (!calibration) {
      return null;
    }

    const lutZero = Math.trunc(calibration.lut_zero_value ?? 0);
    const zeros = calibration.key_zero_values ?? [];
    const deltas = Array.from({ length: KEY_COUNT }, (_, i) => {
      const zero = Math.trunc(zeros[i] ?? lutZero);
      return zero - lutZero;
    });
    const maxDelta = Math.max(1, ...deltas.map((delta) => Math.abs(delta)));

    return { deltas, maxDelta };
  }, [calibrationQ.data]);

  const distanceKeyColorMap = useMemo(() => {
    if (!previewDistancesMm) {
      return undefined;
    }

    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const distMm = previewDistancesMm[i] ?? 0;
      const normalized = distMm / MAX_TRAVEL_MM;
      map[`key-${i}`] = heatmapColor(normalized, isDarkTheme);
    }
    return map;
  }, [isDarkTheme, previewDistancesMm]);

  const distanceLegendMap = useMemo(() => {
    if (!previewDistancesMm) {
      return undefined;
    }

    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      map[`key-${i}`] = (previewDistancesMm[i] ?? 0).toFixed(1);
    }
    return map;
  }, [previewDistancesMm]);

  const heatmapKeyColorMap = useMemo(() => {
    if (!calibrationHeatmapData) {
      return undefined;
    }

    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const delta = calibrationHeatmapData.deltas[i] ?? 0;
      map[`key-${i}`] = calibrationDeltaColor(delta, calibrationHeatmapData.maxDelta, isDarkTheme);
    }
    return map;
  }, [calibrationHeatmapData, isDarkTheme]);

  const heatmapLegendMap = useMemo(() => {
    if (!calibrationHeatmapData) {
      return undefined;
    }

    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const delta = calibrationHeatmapData.deltas[i] ?? 0;
      map[`key-${i}`] = delta > 0 ? `+${delta}` : String(delta);
    }
    return map;
  }, [calibrationHeatmapData]);

  const guidedKeyColorMap = useMemo(() => {
    if (!guidedStatus?.active && !guidedSuccessBlinkActive) {
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
  }, [guidedBlinkOn, guidedStatus, guidedSuccessBlinkActive, isDarkTheme]);

  const keyLegendSlotsMap = useMemo(() => {
    const map: Record<string, Array<string | undefined>> = {};
    const slots = Array.from({ length: 12 }, (_, index) => (index === 0 ? undefined : ""));
    for (const key of previewKeys) {
      map[key.id] = slots;
    }
    return map;
  }, []);

  const distanceLegendClassName = isDarkTheme
    ? "text-[8px] font-mono font-semibold text-white drop-shadow-[0_0_2px_rgba(0,0,0,.8)]"
    : "text-[8px] font-mono font-semibold text-slate-900";

  const heatmapLegendClassName = isDarkTheme
    ? "text-[8px] font-mono font-semibold text-slate-100 drop-shadow-[0_0_2px_rgba(0,0,0,.7)]"
    : "text-[8px] font-mono font-semibold text-slate-900";

  const guidedOverlayActive = Boolean(guidedKeyColorMap);
  const previewKeyColorMap = guidedOverlayActive
    ? guidedKeyColorMap
    : previewMode === "distance"
      ? distanceKeyColorMap
      : heatmapKeyColorMap;
  const previewKeyLegendMap = guidedOverlayActive
    ? undefined
    : previewMode === "distance"
      ? distanceLegendMap
      : heatmapLegendMap;
  const previewLegendClassName = guidedOverlayActive
    ? undefined
    : previewMode === "distance"
      ? distanceLegendClassName
      : heatmapLegendClassName;

  const selectionKeyboard = useMemo(() => (
    <BaseKeyboard
      mode="single"
      onButtonClick={() => {}}
      showLayerSelector={false}
      showRotary={false}
      showTooltips={false}
      keyColorMap={previewKeyColorMap}
      keyLegendMap={previewKeyLegendMap}
      keyLegendSlotsMap={keyLegendSlotsMap}
      keyLegendClassName={previewLegendClassName}
    />
  ), [keyLegendSlotsMap, previewKeyColorMap, previewKeyLegendMap, previewLegendClassName]);

  const applyManualValues = useCallback(async () => {
    if (keyIndex == null || !calibrationQ.data) {
      return;
    }

    const parsedZero = Number.parseInt(manualZeroInput, 10);
    const parsedMax = Number.parseInt(manualMaxInput, 10);

    if (Number.isNaN(parsedZero) || Number.isNaN(parsedMax)) {
      setManualError("Zero and Max must be valid integers.");
      return;
    }

    if (parsedZero < MIN_CALIBRATION_VALUE || parsedZero > MAX_CALIBRATION_VALUE) {
      setManualError(`Zero must be between ${MIN_CALIBRATION_VALUE} and ${MAX_CALIBRATION_VALUE}.`);
      return;
    }

    if (parsedMax < MIN_CALIBRATION_VALUE || parsedMax > MAX_CALIBRATION_VALUE) {
      setManualError(`Max must be between ${MIN_CALIBRATION_VALUE} and ${MAX_CALIBRATION_VALUE}.`);
      return;
    }

    if (parsedMax <= parsedZero) {
      setManualError("Max must be greater than Zero.");
      return;
    }

    setManualError(null);
    await commitCalibrationForSelectedKey(parsedZero, parsedMax, true);
  }, [calibrationQ.data, commitCalibrationForSelectedKey, keyIndex, manualMaxInput, manualZeroInput]);

  const resetSelectedKeyCalibration = useCallback(async () => {
    if (keyIndex == null || !calibrationQ.data) {
      return;
    }

    const resetZero = Math.trunc(calibrationQ.data.lut_zero_value ?? 0);
    const nextMax = calibrationQ.data.key_max_values[keyIndex] ?? 0;
    setManualZeroInput(String(resetZero));
    setManualMaxInput(String(nextMax));
    setManualError(null);
    await commitCalibrationForSelectedKey(resetZero, nextMax, true);
  }, [calibrationQ.data, commitCalibrationForSelectedKey, keyIndex]);

  const restoreAllCalibration = useCallback(async () => {
    if (!calibrationQ.data) {
      return;
    }

    const resetZero = Math.trunc(calibrationQ.data.lut_zero_value ?? 0);
    const zeros = Array.from({ length: KEY_COUNT }, () => resetZero);
    const maxs = Array.from(calibrationQ.data.key_max_values);
    await commitCalibrationArrays(zeros, maxs, true);
  }, [calibrationQ.data, commitCalibrationArrays]);

  const menubar = (
    <>
      <div className="flex items-center gap-2">
        <Badge variant={connected ? "secondary" : "outline"} className="text-xs">
          {connected ? "Device Connected" : "Device Disconnected"}
        </Badge>
        <Badge variant="outline" className="text-xs">
          Selected: {keyIndex == null ? "None" : focusedKeyLabel}
        </Badge>
      </div>
      <div className="flex items-center gap-2">
        <span className="text-xs text-muted-foreground">Distance (mm)</span>
        <Switch
          checked={previewMode === "offset"}
          onCheckedChange={(checked) => setPreviewMode(checked ? "offset" : "distance")}
        />
        <span className="text-xs text-muted-foreground">Calibration (ADC)</span>
        <Button
          variant="outline"
          size="sm"
          className="h-8"
          onClick={() => void qc.invalidateQueries({ queryKey: ["calibration"] })}
        >
          <IconRefresh className="size-4" />
          Reload
        </Button>
      </div>
    </>
  );

  return (
    <KeyboardEditor keyboard={selectionKeyboard} menubar={menubar}>
      <div className="flex flex-col gap-4">
        {(keyIndex == null || guidedState === "running") && (
          <SectionCard title="Calibration Procedure">
            <div className="flex flex-col gap-4">
              <p className="text-sm text-muted-foreground">
                Guided calibration removes the current calibration for all keys, then recomputes zero and max values key by key.
                Start it only when no key is selected, and follow LED prompts until completion.
              </p>

              <div className="flex flex-wrap items-center gap-2">
                {guidedState === "running" ? (
                  <Button variant="destructive" size="sm" onClick={() => void guidedAbort()}>
                    <IconPlayerStop className="size-4" />
                    Abort Guided Calibration
                  </Button>
                ) : (
                  <Button size="sm" disabled={!connected || keyIndex != null} onClick={() => void guidedStart()}>
                    <IconPlayerPlay className="size-4" />
                    Start Guided Calibration
                  </Button>
                )}

                {guidedTargetLabel && (guidedStatus?.phase === 2 || guidedStatus?.phase === 3) && (
                  <Badge variant="outline" className="text-xs">
                    Press: {guidedTargetLabel}
                  </Badge>
                )}
              </div>

              {guidedState !== "idle" && (
                <div className="flex flex-col gap-2">
                  <Progress value={guidedProgress} className="h-2" />
                  <span className="text-xs text-muted-foreground">
                    {guidedState === "running" && `Calibrating... ${guidedProgress}%`}
                    {guidedState === "success" && "Calibration complete."}
                    {guidedState === "error" && "Calibration failed."}
                  </span>
                </div>
              )}
            </div>
          </SectionCard>
        )}

        {keyIndex != null ? (
          <SectionCard title={`Key ${focusedKeyLabel} Calibration`}>
            <div className="flex flex-col gap-4">
              {calibrationQ.isLoading ? (
                <div className="flex flex-col gap-2">
                  <Skeleton className="h-8 w-full" />
                  <Skeleton className="h-8 w-full" />
                  <Skeleton className="h-9 w-40" />
                </div>
              ) : calibrationQ.data ? (
                <>
                  <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
                    <div className="flex flex-col gap-2">
                      <Label className="text-sm" htmlFor="manual-zero-input">
                        Key {focusedKeyLabel} Zero (ADC)
                      </Label>
                      <Input
                        id="manual-zero-input"
                        type="number"
                        inputMode="numeric"
                        min={MIN_CALIBRATION_VALUE}
                        max={MAX_CALIBRATION_VALUE}
                        step={1}
                        disabled={!connected}
                        value={manualZeroInput}
                        onChange={(event) => setManualZeroInput(event.target.value)}
                      />
                    </div>

                    <div className="flex flex-col gap-2">
                      <Label className="text-sm" htmlFor="manual-max-input">
                        Key {focusedKeyLabel} Max (ADC)
                      </Label>
                      <Input
                        id="manual-max-input"
                        type="number"
                        inputMode="numeric"
                        min={MIN_CALIBRATION_VALUE}
                        max={MAX_CALIBRATION_VALUE}
                        step={1}
                        disabled={!connected}
                        value={manualMaxInput}
                        onChange={(event) => setManualMaxInput(event.target.value)}
                      />
                    </div>
                  </div>

                  {manualError && (
                    <p className="text-sm text-destructive">{manualError}</p>
                  )}

                  <div className="flex flex-wrap items-center gap-2">
                    <Button
                      size="sm"
                      disabled={!connected || autoCalibrateMutation.isPending}
                      onClick={() => autoCalibrateMutation.mutate(keyIndex)}
                    >
                      Auto Calibrate Zero
                    </Button>

                    <Button
                      size="sm"
                      variant="secondary"
                      disabled={!connected || autoCalibrateMutation.isPending}
                      onClick={() => void applyManualValues()}
                    >
                      Apply Manual Values
                    </Button>

                    <Button
                      size="sm"
                      variant="destructive"
                      className="w-fit"
                      disabled={!connected || autoCalibrateMutation.isPending}
                      onClick={() => void resetSelectedKeyCalibration()}
                    >
                      Reset Key Calibration
                    </Button>
                  </div>
                </>
              ) : (
                <p className="text-sm text-muted-foreground">Connect device to edit key calibration.</p>
              )}
            </div>
          </SectionCard>
        ) : (
          <SectionCard title="Restore Calibration">
            {calibrationQ.isLoading ? (
              <div className="flex flex-col gap-2">
                <Skeleton className="h-8 w-full" />
                <Skeleton className="h-9 w-56" />
              </div>
            ) : calibrationQ.data ? (
              <div className="flex flex-col gap-3">
                <p className="text-sm text-muted-foreground">
                  This will delete all calibration parameters (zero and max) for every key and restore keyboard-wide reference values.
                </p>
                <Button
                  size="sm"
                  variant="destructive"
                  className="w-fit"
                  disabled={!connected || autoCalibrateMutation.isPending}
                  onClick={() => void restoreAllCalibration()}
                >
                  Restore Calibration (All Keys)
                </Button>
              </div>
            ) : (
              <p className="text-sm text-muted-foreground">Connect device to restore calibration.</p>
            )}
          </SectionCard>
        )}
      </div>
    </KeyboardEditor>
  );
}
