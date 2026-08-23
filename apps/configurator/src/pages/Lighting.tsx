import React, { useState, useRef, useCallback, useMemo, useEffect } from "react";
import { isTauri } from "@tauri-apps/api/core";
import { useThrottledCall } from "@/hooks/use-throttled-call";
import { usePageVisible } from "@/hooks/use-page-visible";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useOptimisticMutation } from "@/hooks/use-optimistic-mutation";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice, type LedIdleOptions } from "@/lib/kbhe/device";
import { requireDeviceSuccess } from "@/lib/kbhe/mutation-result";
import {
  patchActiveAppProfileLedEffectParams,
  patchActiveAppProfileLedIdleOptions,
  patchActiveAppProfileLedPixelBatch,
  patchActiveAppProfileLedPixels,
  patchActiveAppProfileLedSnapshot,
} from "@/lib/kbhe/profile-snapshot-store";
import {
  LEDEffect, KEY_COUNT, LED_EFFECT_NAMES, LED_PARAM_TYPES,
  LED_EFFECT_PARAM_SPEED, LED_EFFECT_PARAM_COUNT,
  LED_USB_SUSPEND_RGB_OFF_DEFAULT,
} from "@/lib/kbhe/protocol";
import { EFFECT_PARAM_ENUM_OPTIONS, EFFECT_PARAM_NAMES } from "@/lib/kbhe/effectParamNames";
import BaseKeyboard from "@/components/baseKeyboard";
import { LibhmkLighting } from "@/components/LibhmkLighting";
import {
  libhmkRgbBridge,
  rgbBridgeSerialNumber,
  selectRgbBridgeDevice,
} from "@/lib/kbhe/rgb-bridge";

import { queryKeys } from "@/lib/query/keys";
import { AutosaveStatus, useAutosave } from "@/components/AutosaveStatus";
import { KeyboardEditor } from "@/components/keyboard-editor";
import { SectionCard, FormRow, FormRows } from "@/components/shared/SectionCard";
import { PageSection } from "@/components/shared/PageLayout";
import { EmptyState } from "@/components/shared/EmptyState";
import { SliderField } from "@/components/shared/SliderField";
import { Toolbar, ToolbarStat } from "@/components/shared/Toolbar";
import { Badge } from "@/components/ui/badge";
import { ColorPicker, type RGBColor } from "@/components/color-picker";
import { Switch } from "@/components/ui/switch";
import { CommitSlider } from "@/components/ui/commit-slider";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Label } from "@/components/ui/label";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { Input } from "@/components/ui/input";
import {
  IconAdjustments,
  IconArrowsExchange,
  IconBrush,
  IconBulb,
  IconEye,
  IconMoon,
  IconMusic,
  IconSearch,
  IconSparkles,
  IconTrash,
  IconUpload,
  IconDownload,
  IconFileImport,
  IconFileExport,
  IconRestore,
} from "@tabler/icons-react";

// ---------------------------------------------------------------------------
// Effect list — built from LED_EFFECT_NAMES
// ---------------------------------------------------------------------------

const EFFECT_CANONICAL_ID: Partial<Record<number, number>> = {
  [LEDEffect.SOLID_REACTIVE_MULTI_WIDE]: LEDEffect.SOLID_REACTIVE_WIDE,
  [LEDEffect.SOLID_REACTIVE_MULTI_CROSS]: LEDEffect.SOLID_REACTIVE_CROSS,
  [LEDEffect.SOLID_REACTIVE_MULTI_NEXUS]: LEDEffect.SOLID_REACTIVE_NEXUS,
  [LEDEffect.MULTI_SPLASH]: LEDEffect.SPLASH,
  [LEDEffect.SOLID_MULTI_SPLASH]: LEDEffect.SOLID_SPLASH,
  [LEDEffect.STARLIGHT_DUAL_HUE]: LEDEffect.STARLIGHT_DUAL_SAT,
  [LEDEffect.COLORBAND_VAL]: LEDEffect.COLORBAND_SAT,
  [LEDEffect.COLORBAND_PINWHEEL_VAL]: LEDEffect.COLORBAND_PINWHEEL_SAT,
  [LEDEffect.COLORBAND_SPIRAL_VAL]: LEDEffect.COLORBAND_SPIRAL_SAT,
};

const HIDDEN_EFFECT_IDS = new Set<number>([
  LEDEffect.SOLID_REACTIVE_MULTI_WIDE,
  LEDEffect.SOLID_REACTIVE_MULTI_CROSS,
  LEDEffect.SOLID_REACTIVE_MULTI_NEXUS,
  LEDEffect.MULTI_SPLASH,
  LEDEffect.SOLID_MULTI_SPLASH,
  LEDEffect.STARLIGHT_DUAL_HUE,
  LEDEffect.COLORBAND_VAL,
  LEDEffect.COLORBAND_PINWHEEL_VAL,
  LEDEffect.COLORBAND_SPIRAL_VAL,
]);

function canonicalEffectId(id: number): number {
  return EFFECT_CANONICAL_ID[id] ?? id;
}

const ALL_EFFECTS = Object.entries(LED_EFFECT_NAMES)
  .map(([idStr, name]) => ({ id: Number(idStr), name }))
  .filter((effect) => !HIDDEN_EFFECT_IDS.has(effect.id))
  .sort((a, b) => a.id - b.id);

interface EffectCategoryDef {
  key: string;
  title: string;
  ids: number[];
}

interface EffectCategory {
  key: string;
  title: string;
  effects: Array<{ id: number; name: string }>;
}

const EFFECT_CATEGORY_DEFS: EffectCategoryDef[] = [
  {
    key: "essentials",
    title: "Essentials",
    ids: [
      LEDEffect.NONE,
      LEDEffect.THIRD_PARTY,
      LEDEffect.SOLID_COLOR,
      LEDEffect.ALPHA_MODS,
      LEDEffect.GRADIENT_UP_DOWN,
      LEDEffect.GRADIENT_LEFT_RIGHT,
      LEDEffect.BREATHING,
    ],
  },
  {
    key: "reactive",
    title: "Reactive & Typing",
    ids: [
      LEDEffect.IMPACT_RAINBOW,
      LEDEffect.REACTIVE_GHOST,
      LEDEffect.TYPING_HEATMAP,
      LEDEffect.SOLID_REACTIVE_SIMPLE,
      LEDEffect.SOLID_REACTIVE,
      LEDEffect.SOLID_REACTIVE_WIDE,
      LEDEffect.SOLID_REACTIVE_CROSS,
      LEDEffect.SOLID_REACTIVE_NEXUS,
      LEDEffect.SPLASH,
      LEDEffect.SOLID_SPLASH,
    ],
  },
  {
    key: "audio-input",
    title: "Audio & Input",
    ids: [
      LEDEffect.AUDIO_SPECTRUM,
      LEDEffect.BASS_RIPPLE,
      LEDEffect.DISTANCE_SENSOR,
      LEDEffect.KEY_STATE_DEMO,
    ],
  },
  {
    key: "rainbow-cycles",
    title: "Rainbow & Cycles",
    ids: [
      LEDEffect.BREATHING_RAINBOW,
      LEDEffect.COLOR_CYCLE,
      LEDEffect.CYCLE_ALL,
      LEDEffect.CYCLE_LEFT_RIGHT,
      LEDEffect.CYCLE_UP_DOWN,
      LEDEffect.CYCLE_OUT_IN,
      LEDEffect.CYCLE_PINWHEEL,
      LEDEffect.CYCLE_SPIRAL,
      LEDEffect.CYCLE_OUT_IN_DUAL,
      LEDEffect.RAINBOW_BEACON,
      LEDEffect.RAINBOW_PINWHEELS,
      LEDEffect.RAINBOW_MOVING_CHEVRON,
      LEDEffect.HUE_BREATHING,
      LEDEffect.HUE_PENDULUM,
      LEDEffect.HUE_WAVE,
      LEDEffect.DUAL_BEACON,
    ],
  },
  {
    key: "colorband",
    title: "Colorband",
    ids: [
      LEDEffect.COLORBAND_SAT,
      LEDEffect.COLORBAND_VAL,
      LEDEffect.COLORBAND_PINWHEEL_SAT,
      LEDEffect.COLORBAND_PINWHEEL_VAL,
      LEDEffect.COLORBAND_SPIRAL_SAT,
      LEDEffect.COLORBAND_SPIRAL_VAL,
    ],
  },
  {
    key: "ambient",
    title: "Ambient & Particles",
    ids: [
      LEDEffect.PLASMA,
      LEDEffect.FIRE,
      LEDEffect.OCEAN,
      LEDEffect.SPARKLE,
      LEDEffect.RIVERFLOW,
      LEDEffect.FLOWER_BLOOMING,
      LEDEffect.RAINDROPS,
      LEDEffect.JELLYBEAN_RAINDROPS,
      LEDEffect.PIXEL_RAIN,
      LEDEffect.PIXEL_FLOW,
      LEDEffect.PIXEL_FRACTAL,
      LEDEffect.DIGITAL_RAIN,
      LEDEffect.STARLIGHT_SMOOTH,
      LEDEffect.STARLIGHT,
      LEDEffect.STARLIGHT_DUAL_SAT,
      LEDEffect.STARLIGHT_DUAL_HUE,
    ],
  },
];

function groupEffectsByCategory(effects: Array<{ id: number; name: string }>): EffectCategory[] {
  const byId = new Map(effects.map((effect) => [effect.id, effect]));
  const used = new Set<number>();
  const categories: EffectCategory[] = [];

  for (const definition of EFFECT_CATEGORY_DEFS) {
    const items = definition.ids
      .map((id) => byId.get(id))
      .filter((effect): effect is { id: number; name: string } => Boolean(effect));

    if (items.length > 0) {
      categories.push({ key: definition.key, title: definition.title, effects: items });
      for (const item of items) {
        used.add(item.id);
      }
    }
  }

  const uncategorized = effects
    .filter((effect) => !used.has(effect.id))
    .sort((a, b) => a.name.localeCompare(b.name));

  if (uncategorized.length > 0) {
    categories.push({ key: "other", title: "Other", effects: uncategorized });
  }

  return categories;
}

const MATRIX_PREVIEW_FRAME_MS = 33;

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
  const canonicalId = canonicalEffectId(id);
  return LED_EFFECT_NAMES[canonicalId] ?? `Mode ${canonicalId}`;
}

// ---------------------------------------------------------------------------
// Matrix Keyboard sub-component
// ---------------------------------------------------------------------------

function MatrixKeyboard({
  pixelColors,
  connected,
  onPaint,
  onStrokeEnd,
}: {
  pixelColors: RGBColor[];
  connected: boolean;
  onPaint: (idx: number) => void;
  onStrokeEnd?: () => void;
}) {
  const rootRef = useRef<HTMLDivElement | null>(null);
  const isPaintingRef = useRef(false);
  const paintedDuringStrokeRef = useRef<Set<number>>(new Set());

  const keyColorMap = useMemo(() => {
    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const c = pixelColors[i];
      if (c) map[`key-${i}`] = rgbToHex(c.r, c.g, c.b);
    }
    return map;
  }, [pixelColors]);

  const paintKeyId = useCallback((keyId: string | null) => {
    if (!connected || !keyId?.startsWith("key-")) return;
    const idx = parseInt(keyId.replace("key-", ""), 10);
    if (!Number.isFinite(idx) || idx < 0 || idx >= KEY_COUNT) return;

    if (isPaintingRef.current) {
      if (paintedDuringStrokeRef.current.has(idx)) return;
      paintedDuringStrokeRef.current.add(idx);
    }

    onPaint(idx);
  }, [connected, onPaint]);

  const paintFromPoint = useCallback((clientX: number, clientY: number) => {
    const root = rootRef.current;
    if (!root) return;
    const target = document.elementFromPoint(clientX, clientY);
    if (!(target instanceof Element) || !root.contains(target)) return;
    const keyNode = target.closest("[data-kle-key-id]");
    const keyId = keyNode?.getAttribute("data-kle-key-id") ?? null;
    paintKeyId(keyId);
  }, [paintKeyId]);

  const handlePointerDown = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    if (!connected) return;
    event.preventDefault();
    isPaintingRef.current = true;
    paintedDuringStrokeRef.current.clear();
    event.currentTarget.setPointerCapture?.(event.pointerId);
    paintFromPoint(event.clientX, event.clientY);
  }, [connected, paintFromPoint]);

  const handlePointerMove = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    if (!connected || !isPaintingRef.current) return;
    paintFromPoint(event.clientX, event.clientY);
  }, [connected, paintFromPoint]);

  const endPaintStroke = useCallback(() => {
    isPaintingRef.current = false;
    paintedDuringStrokeRef.current.clear();
    onStrokeEnd?.();
  }, [onStrokeEnd]);

  return (
    <div
      ref={rootRef}
      className="h-full w-full select-none touch-none"
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={endPaintStroke}
      onPointerCancel={endPaintStroke}
      onLostPointerCapture={endPaintStroke}
      onContextMenu={(event) => event.preventDefault()}
    >
      <BaseKeyboard
        mode="single"
        onButtonClick={() => { }}
        showLayerSelector={false}
        showRotary={false}
        interactive={false}
        showTooltips={false}
        keyColorMap={keyColorMap}
      />
    </div>
  );
}

const LIVE_MATRIX_POLL_MS = 60;
// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

function NativeLighting() {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const pageVisible = usePageVisible();
  const qc = useQueryClient();
  const { saveState, markSaving, markSaved, markError } = useAutosave();
  const [paintColor, setPaintColor] = useState<RGBColor>({ r: 255, g: 0, b: 0 });
  const [effectSearch, setEffectSearch] = useState("");
  const [liveKeyboardPreviewEnabled, setLiveKeyboardPreviewEnabled] = useState(true);
  const [liveParamValues, setLiveParamValues] = useState<Record<number, number>>({});
  const fileInputRef = useRef<HTMLInputElement>(null);

  const filteredEffects = useMemo(() => {
    const query = effectSearch.trim().toLowerCase();
    if (!query) {
      return ALL_EFFECTS;
    }

    return ALL_EFFECTS.filter((effect) =>
      effect.name.toLowerCase().includes(query) || String(effect.id).includes(query),
    );
  }, [effectSearch]);

  const groupedEffects = useMemo(
    () => groupEffectsByCategory(filteredEffects),
    [filteredEffects],
  );

  // ---- Queries ----

  const enabledQ = useQuery({
    queryKey: queryKeys.led.enabled(),
    queryFn: () => kbheDevice.ledGetEnabled(),
    enabled: connected,
    placeholderData: (previous) => previous,
  });

  const brightnessQ = useQuery({
    queryKey: queryKeys.led.brightness(),
    queryFn: () => kbheDevice.ledGetBrightness(),
    enabled: connected,
    placeholderData: (previous) => previous,
  });

  const effectQ = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled: connected,
    placeholderData: (previous) => previous,
  });

  const fpsQ = useQuery({
    queryKey: queryKeys.led.fpsLimit(),
    queryFn: () => kbheDevice.getLedFpsLimit(),
    enabled: connected,
    placeholderData: (previous) => previous,
  });

  const idleOptionsQ = useQuery({
    queryKey: queryKeys.led.idleOptions(),
    queryFn: () => kbheDevice.getLedIdleOptions(),
    enabled: connected,
    placeholderData: (previous) => previous,
  });

  const currentEffect = effectQ.data ?? LEDEffect.NONE;
  const currentEffectForSelection = canonicalEffectId(currentEffect);

  useEffect(() => {
    setLiveParamValues({});
  }, [currentEffect]);

  const paramsQ = useQuery({
    queryKey: queryKeys.led.effectParams(currentEffect),
    queryFn: () => kbheDevice.getLedEffectParams(currentEffect),
    enabled: connected,
    placeholderData: (previous) => previous,
  });

  const schemaQ = useQuery({
    queryKey: ["led", "effectSchema", currentEffect],
    queryFn: () => kbheDevice.getLedEffectSchema(currentEffect),
    enabled: connected,
    staleTime: 60_000,
    placeholderData: (previous) => previous,
  });

  const isMatrixMode = currentEffect === LEDEffect.MATRIX;
  const liveMatrixPolling = connected && !isMatrixMode && pageVisible && liveKeyboardPreviewEnabled;

  const allPixelsQ = useQuery({
    queryKey: queryKeys.led.allPixels(),
    queryFn: () => kbheDevice.ledDownloadAll(),
    enabled: connected,
    placeholderData: (previous) => previous,
    refetchInterval: false,
    refetchOnWindowFocus: false,
    refetchIntervalInBackground: false,
  });

  const [matrixPixels, setMatrixPixels] = useState<number[] | null>(null);
  const matrixPixelsRef = useRef<number[] | null>(null);
  const matrixPreviewPendingRef = useRef<number[] | null>(null);
  const matrixPreviewTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const matrixLastPreviewPushRef = useRef(0);

  const flushMatrixPreview = useCallback(() => {
    matrixPreviewTimerRef.current = null;
    const pending = matrixPreviewPendingRef.current;
    if (!pending) {
      return;
    }
    matrixPreviewPendingRef.current = null;
    matrixLastPreviewPushRef.current = performance.now();
    setMatrixPixels(Array.from(pending));
  }, []);

  const enqueueMatrixPreview = useCallback((frame: number[]) => {
    matrixPreviewPendingRef.current = frame;

    const elapsed = performance.now() - matrixLastPreviewPushRef.current;
    const waitMs = Math.max(0, MATRIX_PREVIEW_FRAME_MS - elapsed);

    if (waitMs <= 0 && !matrixPreviewTimerRef.current) {
      flushMatrixPreview();
      return;
    }

    if (!matrixPreviewTimerRef.current) {
      matrixPreviewTimerRef.current = setTimeout(flushMatrixPreview, waitMs);
    }
  }, [flushMatrixPreview]);

  useEffect(() => {
    return () => {
      if (matrixPreviewTimerRef.current) {
        clearTimeout(matrixPreviewTimerRef.current);
      }
    };
  }, []);

  const applyIncomingMatrixFrame = useCallback((frame: ArrayLike<number>) => {
    const next = Array.from(frame);
    const prev = matrixPixelsRef.current;
    if (prev && prev.length === next.length) {
      let changed = false;
      for (let i = 0; i < next.length; i += 1) {
        if (prev[i] !== next[i]) {
          changed = true;
          break;
        }
      }
      if (!changed) {
        return;
      }
    }

    matrixPixelsRef.current = next;
    if (!isMatrixMode) {
      enqueueMatrixPreview(next);
      return;
    }
    setMatrixPixels(next);
  }, [enqueueMatrixPreview, isMatrixMode]);

  useEffect(() => {
    if (!allPixelsQ.data) {
      matrixPixelsRef.current = null;
      setMatrixPixels(null);
      return;
    }

    applyIncomingMatrixFrame(allPixelsQ.data);
  }, [allPixelsQ.data, applyIncomingMatrixFrame]);

  useEffect(() => {
    if (!liveMatrixPolling) {
      return;
    }

    let disposed = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const tick = async () => {
      const startedAt = performance.now();
      try {
        const frame = await kbheDevice.ledDownloadAll();
        if (disposed || !frame) {
          return;
        }
        applyIncomingMatrixFrame(frame);
      } catch {
        // Ignore transient read errors while live preview polling.
      } finally {
        if (!disposed) {
          const elapsed = performance.now() - startedAt;
          const waitMs = Math.max(0, LIVE_MATRIX_POLL_MS - elapsed);
          timer = setTimeout(() => void tick(), waitMs);
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
  }, [applyIncomingMatrixFrame, liveMatrixPolling]);

  useEffect(() => {
    if (!connected || !isMatrixMode) {
      return;
    }

    void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() });
  }, [connected, isMatrixMode, qc]);

  const pixelColors = useMemo(
    () => (matrixPixels ? pixelsToRgbArray(matrixPixels) : null),
    [matrixPixels],
  );

  const updateMatrixPixelsLocal = useCallback((next: number[]) => {
    const snapshot = Array.from(next);
    matrixPixelsRef.current = snapshot;
    setMatrixPixels(snapshot);
    qc.setQueryData(queryKeys.led.allPixels(), snapshot);
  }, [qc]);

  // ---- Mutations ----

  const enabledMut = useMutation({
    mutationFn: async (v: boolean) => {
      markSaving();
      const ok = await kbheDevice.ledSetEnabled(v);
      requireDeviceSuccess(ok, "LED setting");
      patchActiveAppProfileLedSnapshot({ enabled: v });
    },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.enabled() }); },
    onError: markError,
  });

  const brightnessMut = useOptimisticMutation<number, number, void>({
    queryKey: queryKeys.led.brightness(),
    mutationFn: async (v) => {
      markSaving();
      const ok = await kbheDevice.ledSetBrightness(v);
      requireDeviceSuccess(ok, "LED brightness");
      patchActiveAppProfileLedSnapshot({ brightness: v });
    },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: markSaved,
    onError: markError,
  });

  const effectMut = useMutation({
    mutationFn: async (v: number) => {
      markSaving();
      const ok = await kbheDevice.setLedEffect(v);
      requireDeviceSuccess(ok, "LED effect");
      patchActiveAppProfileLedSnapshot({ effectMode: v });
    },
    onSuccess: () => { markSaved(); void qc.invalidateQueries({ queryKey: queryKeys.led.effect() }); },
    onError: markError,
  });

  const fpsMut = useOptimisticMutation<number, number, void>({
    queryKey: queryKeys.led.fpsLimit(),
    mutationFn: async (v) => {
      markSaving();
      const ok = await kbheDevice.setLedFpsLimit(v);
      requireDeviceSuccess(ok, "LED FPS limit");
      patchActiveAppProfileLedSnapshot({ fpsLimit: v });
    },
    optimisticUpdate: (_cur, v) => v,
    onSuccess: markSaved,
    onError: markError,
  });

  const paramsDesiredRef = useRef<{ effectMode: number; params: number[] } | null>(null);
  const paramsMut = useOptimisticMutation<number[], number[], void>({
    queryKey: queryKeys.led.effectParams(currentEffect),
    mutationFn: async (params) => {
      markSaving();
      const ok = await kbheDevice.setLedEffectParams(currentEffect, params);
      requireDeviceSuccess(ok, "LED effect parameters");
      patchActiveAppProfileLedEffectParams(currentEffect, params);
    },
    optimisticUpdate: (_cur, params) => params,
    onSuccess: markSaved,
    onError: () => {
      paramsDesiredRef.current = null;
      markError();
    },
  });

  const commitEffectParamUpdates = useCallback((updates: Readonly<Record<number, number>>) => {
    const desired = paramsDesiredRef.current;
    const base = desired?.effectMode === currentEffect ? desired.params : paramsQ.data;
    if (!base) return;
    const next = [...base];
    for (const [rawIndex, value] of Object.entries(updates)) {
      const index = Number(rawIndex);
      if (Number.isInteger(index) && index >= 0 && index < LED_EFFECT_PARAM_COUNT) {
        next[index] = value & 0xff;
      }
    }
    paramsDesiredRef.current = { effectMode: currentEffect, params: next };
    paramsMut.mutate(next);
  }, [currentEffect, paramsMut, paramsQ.data]);

  const commitEffectParams = useCallback((params: number[]) => {
    const next = [...params];
    paramsDesiredRef.current = { effectMode: currentEffect, params: next };
    paramsMut.mutate(next);
  }, [currentEffect, paramsMut]);

  useEffect(() => {
    if (!paramsMut.isPending) {
      paramsDesiredRef.current = paramsQ.data
        ? { effectMode: currentEffect, params: [...paramsQ.data] }
        : null;
    }
  }, [currentEffect, paramsMut.isPending, paramsQ.data]);

  const idleDesiredRef = useRef<LedIdleOptions | null>(null);
  const idleOptionsMut = useOptimisticMutation<
    LedIdleOptions | null,
    LedIdleOptions,
    void
  >({
    queryKey: queryKeys.led.idleOptions(),
    mutationFn: async (next) => {
      markSaving();
      const ok = await kbheDevice.setLedIdleOptions(
        next.idle_timeout_seconds,
        next.allow_system_when_disabled,
        next.third_party_stream_counts_as_activity,
        next.usb_suspend_rgb_off,
      );
      requireDeviceSuccess(ok, "LED idle options");
      patchActiveAppProfileLedIdleOptions(next);
    },
    optimisticUpdate: (_current, next) => next,
    onSuccess: markSaved,
    onError: () => {
      idleDesiredRef.current = null;
      markError();
    },
  });

  const commitIdleOptions = useCallback((patch: Partial<LedIdleOptions>) => {
    const base = idleDesiredRef.current ?? idleOptionsQ.data ?? {
      idle_timeout_seconds: 0,
      allow_system_when_disabled: false,
      third_party_stream_counts_as_activity: false,
      usb_suspend_rgb_off: LED_USB_SUSPEND_RGB_OFF_DEFAULT,
    };
    const next = { ...base, ...patch };
    idleDesiredRef.current = next;
    idleOptionsMut.mutate(next);
  }, [idleOptionsMut, idleOptionsQ.data]);

  useEffect(() => {
    if (!idleOptionsMut.isPending) {
      idleDesiredRef.current = idleOptionsQ.data ?? null;
    }
  }, [idleOptionsMut.isPending, idleOptionsQ.data]);

  const canResetEffectParams =
    connected && !!schemaQ.data && !!paramsQ.data && !paramsMut.isPending;
  const ledIdleTimeoutSeconds = idleOptionsQ.data?.idle_timeout_seconds ?? 0;
  const allowSystemWhenDisabled = idleOptionsQ.data?.allow_system_when_disabled ?? false;
  const thirdPartyStreamCountsAsActivity =
    idleOptionsQ.data?.third_party_stream_counts_as_activity ?? false;
  const usbSuspendRgbOff =
    idleOptionsQ.data?.usb_suspend_rgb_off ?? LED_USB_SUSPEND_RGB_OFF_DEFAULT;

  const handleResetEffectParams = useCallback(() => {
    if (!schemaQ.data || !paramsQ.data) {
      return;
    }

    const next = [...paramsQ.data];
    const descriptors = schemaQ.data.descriptors ?? [];

    for (const descriptor of descriptors) {
      if (descriptor.type === LED_PARAM_TYPES.NONE) {
        continue;
      }
      if (descriptor.id < 0 || descriptor.id >= LED_EFFECT_PARAM_COUNT) {
        continue;
      }
      next[descriptor.id] = descriptor.default_val & 0xff;
    }

    while (next.length < LED_EFFECT_PARAM_COUNT) {
      next.push(0);
    }

    setLiveParamValues({});
    commitEffectParams(next);
  }, [commitEffectParams, paramsQ.data, schemaQ.data]);

  const fillMut = useMutation({
    mutationFn: async (c: RGBColor) => {
      markSaving();
      const ok = await kbheDevice.ledFill(c.r, c.g, c.b);
      requireDeviceSuccess(ok, "LED fill");
    },
    onSuccess: (_result, c) => {
      const filled = new Array(KEY_COUNT * 3).fill(0);
      for (let i = 0; i < KEY_COUNT; i++) {
        const offset = i * 3;
        filled[offset] = c.r & 0xff;
        filled[offset + 1] = c.g & 0xff;
        filled[offset + 2] = c.b & 0xff;
      }
      updateMatrixPixelsLocal(filled);
      patchActiveAppProfileLedPixels(filled);
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() });
    },
    onError: markError,
  });

  const clearMut = useMutation({
    mutationFn: async () => {
      markSaving();
      const ok = await kbheDevice.ledClear();
      requireDeviceSuccess(ok, "LED clear");
    },
    onSuccess: () => {
      const cleared = new Array(KEY_COUNT * 3).fill(0);
      updateMatrixPixelsLocal(cleared);
      patchActiveAppProfileLedPixels(cleared);
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() });
    },
    onError: markError,
  });

  const uploadMut = useMutation({
    mutationFn: async (pixels: number[]) => {
      markSaving();
      const ok = await kbheDevice.ledUploadAll(pixels);
      requireDeviceSuccess(ok, "LED matrix upload");
    },
    onSuccess: (_result, pixels) => {
      updateMatrixPixelsLocal(pixels);
      patchActiveAppProfileLedPixels(pixels);
      markSaved();
      void qc.invalidateQueries({ queryKey: queryKeys.led.allPixels() });
    },
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
    const ok = await kbheDevice.ledSetBrightness(v);
    if (ok) patchActiveAppProfileLedSnapshot({ brightness: v });
  });

  const liveFps = useThrottledCall(async (v: number) => {
    const ok = await kbheDevice.setLedFpsLimit(v);
    if (ok) patchActiveAppProfileLedSnapshot({ fpsLimit: v });
  });

  const liveParams = useThrottledCall(async (params: number[]) => {
    const ok = await kbheDevice.setLedEffectParams(currentEffect, params);
    if (ok) patchActiveAppProfileLedEffectParams(currentEffect, params);
  });

  const liveEffectParamUpdates = useCallback((updates: Readonly<Record<number, number>>) => {
    const desired = paramsDesiredRef.current;
    const base = desired?.effectMode === currentEffect ? desired.params : paramsQ.data;
    if (!base) return;
    const next = [...base];
    for (const [rawIndex, value] of Object.entries(updates)) {
      const index = Number(rawIndex);
      if (Number.isInteger(index) && index >= 0 && index < LED_EFFECT_PARAM_COUNT) {
        next[index] = value & 0xff;
      }
    }
    liveParams(next);
  }, [currentEffect, liveParams, paramsQ.data]);

  const matrixSetPixelPendingRef = useRef<Map<number, RGBColor>>(new Map());
  const matrixSetPixelInFlightRef = useRef(false);
  const matrixFlushRequestedRef = useRef(false);

  const flushMatrixSetPixelQueue = useCallback(async () => {
    if (matrixSetPixelInFlightRef.current) {
      return;
    }

    matrixSetPixelInFlightRef.current = true;
    let transportFailed = false;
    try {
      while (matrixSetPixelPendingRef.current.size > 0) {
        const batch = Array.from(matrixSetPixelPendingRef.current.entries());
        matrixSetPixelPendingRef.current.clear();
        const appliedBatch: Array<readonly [number, RGBColor]> = [];

        for (let batchIndex = 0; batchIndex < batch.length; batchIndex += 1) {
          const [index, color] = batch[batchIndex];
          let ok = false;
          let transportError: unknown;
          try {
            ok = await kbheDevice.ledSetPixel(index, color.r, color.g, color.b);
          } catch (error) {
            transportError = error;
          }
          if (!ok) {
            for (const [retryIndex, retryColor] of batch.slice(batchIndex)) {
              if (!matrixSetPixelPendingRef.current.has(retryIndex)) {
                matrixSetPixelPendingRef.current.set(retryIndex, retryColor);
              }
            }
            transportFailed = true;
            markError(transportError ?? new Error(`Unable to paint LED ${index + 1}`));
            break;
          }
          appliedBatch.push([index, color]);
        }
        patchActiveAppProfileLedPixelBatch(appliedBatch);
        if (transportFailed) break;
      }
    } catch (error) {
      transportFailed = true;
      markError(error);
    } finally {
      matrixSetPixelInFlightRef.current = false;
      const requestedWhileBusy = matrixFlushRequestedRef.current;
      matrixFlushRequestedRef.current = false;
      if (matrixSetPixelPendingRef.current.size > 0 && (!transportFailed || requestedWhileBusy)) {
        void flushMatrixSetPixelQueue();
      }
    }
  }, [markError]);

  const paintMatrixKey = useCallback((index: number) => {
    if (!connected || !isMatrixMode || index < 0 || index >= KEY_COUNT) {
      return;
    }

    if (!matrixPixelsRef.current) {
      matrixPixelsRef.current = Array.from(allPixelsQ.data ?? new Array(KEY_COUNT * 3).fill(0));
    }

    const next = matrixPixelsRef.current;
    const offset = index * 3;
    next[offset] = paintColor.r & 0xff;
    next[offset + 1] = paintColor.g & 0xff;
    next[offset + 2] = paintColor.b & 0xff;

    matrixSetPixelPendingRef.current.set(index, {
      r: paintColor.r & 0xff,
      g: paintColor.g & 0xff,
      b: paintColor.b & 0xff,
    });

    enqueueMatrixPreview(next);
    if (matrixSetPixelInFlightRef.current) {
      matrixFlushRequestedRef.current = true;
    }
    void flushMatrixSetPixelQueue();
  }, [allPixelsQ.data, connected, enqueueMatrixPreview, flushMatrixSetPixelQueue, isMatrixMode, paintColor.b, paintColor.g, paintColor.r]);

  const handleMatrixStrokeEnd = useCallback(() => {
    if (matrixSetPixelInFlightRef.current) {
      matrixFlushRequestedRef.current = true;
    }
    void flushMatrixSetPixelQueue();
  }, [flushMatrixSetPixelQueue]);

  // ---- File I/O ----

  const handleExport = useCallback(() => {
    const brightness = brightnessQ.data ?? 128;
    const pixels = matrixPixels ?? allPixelsQ.data ?? new Array(KEY_COUNT * 3).fill(0);
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
  }, [brightnessQ.data, matrixPixels, allPixelsQ.data]);

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

  function renderEffectParams() {
    const schema = schemaQ.data;
    const paramsLoaded = paramsQ.data != null;
    const params = paramsQ.data ?? new Array(16).fill(0);
    const descriptors = schema?.descriptors ?? [];

    const activeDescriptors = descriptors.filter((d) => d.type !== LED_PARAM_TYPES.NONE);

    if (!schemaQ.isLoading && activeDescriptors.length === 0) {
      return (
        <p className="text-sm text-muted-foreground py-2">
          This effect has no additional tuning controls.
        </p>
      );
    }

    if (schemaQ.isLoading) {
      return <p className="text-xs text-muted-foreground">Loading schema...</p>;
    }

    // Group consecutive COLOR descriptors into a single ColorPicker
    const rendered: React.ReactNode[] = [];
    let i = 0;
    while (i < activeDescriptors.length) {
      const desc = activeDescriptors[i];
      const label = desc.id === LED_EFFECT_PARAM_SPEED
        ? "Speed"
        : (EFFECT_PARAM_NAMES[currentEffect]?.[desc.id] ?? `Param ${desc.id}`);

      if (desc.type === LED_PARAM_TYPES.COLOR) {
        // Collect R, G, B triplet
        const rDesc = desc;
        const gDesc = activeDescriptors[i + 1];
        const bDesc = activeDescriptors[i + 2];
        if (gDesc?.type === LED_PARAM_TYPES.COLOR && bDesc?.type === LED_PARAM_TYPES.COLOR) {
          const rVal = params[rDesc.id] ?? rDesc.default_val;
          const gVal = params[gDesc.id] ?? gDesc.default_val;
          const bVal = params[bDesc.id] ?? bDesc.default_val;
          const color: RGBColor = { r: rVal, g: gVal, b: bVal };
          rendered.push(
            <div key={`color-${rDesc.id}`} className="flex items-center justify-between">
              <Label className="text-sm">Color</Label>
              <ColorPicker
                color={color}
                onLiveChange={(c) => {
                  liveEffectParamUpdates({
                    [rDesc.id]: c.r,
                    [gDesc.id]: c.g,
                    [bDesc.id]: c.b,
                  });
                }}
                onChange={(c) => {
                  void liveParams.cancelAndWait().then(() => {
                    commitEffectParamUpdates({
                      [rDesc.id]: c.r,
                      [gDesc.id]: c.g,
                      [bDesc.id]: c.b,
                    });
                  });
                }}
              />
            </div>,
          );
          i += 3;
          continue;
        }
      }

      if (desc.type === LED_PARAM_TYPES.BOOL) {
        const checked = (params[desc.id] ?? desc.default_val) > 0;
        rendered.push(
          <div key={desc.id} className="flex items-center justify-between">
            <Label className="text-sm">{label}</Label>
            <Switch
              checked={checked}
              disabled={!connected || !paramsLoaded}
              onCheckedChange={(v) => {
                commitEffectParamUpdates({ [desc.id]: v ? 1 : 0 });
              }}
            />
          </div>,
        );
        i++;
        continue;
      }

      {
        const explicitEnumOptions = EFFECT_PARAM_ENUM_OPTIONS[currentEffect]?.[desc.id];
        const fallbackEnumOptions =
          desc.type === LED_PARAM_TYPES.ENUM &&
            desc.max >= desc.min &&
            desc.max - desc.min <= 16
            ? Array.from({ length: desc.max - desc.min + 1 }, (_, idx) => {
              const value = desc.min + idx;
              return { value, label: `Option ${value}` };
            })
            : undefined;
        const enumOptions = explicitEnumOptions ?? fallbackEnumOptions;

        if (enumOptions && enumOptions.length > 0) {
          const rawValue = params[desc.id] ?? desc.default_val;
          const selectedValue = enumOptions.some((opt) => opt.value === rawValue)
            ? rawValue
            : enumOptions[0].value;
          const selectedOptionLabel =
            enumOptions.find((opt) => opt.value === selectedValue)?.label ?? `Option ${selectedValue}`;

          rendered.push(
            <div key={desc.id} className="flex items-center justify-between gap-3">
              <Label className="text-sm">{label}</Label>
              <Select
                value={String(selectedValue)}
                disabled={!connected || !paramsLoaded}
                onValueChange={(value) => {
                  const parsed = Number(value);
                  if (!Number.isFinite(parsed)) {
                    return;
                  }
                  commitEffectParamUpdates({ [desc.id]: parsed });
                }}
              >
                <SelectTrigger className="h-8 w-56 text-sm">
                  <SelectValue>{selectedOptionLabel}</SelectValue>
                </SelectTrigger>
                <SelectContent>
                  <SelectGroup>
                    {enumOptions.map((option) => (
                      <SelectItem key={`${desc.id}-${option.value}`} value={String(option.value)}>
                        {option.label}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
            </div>,
          );
          i++;
          continue;
        }
      }

      // U8 and HUE both render as sliders
      const val = params[desc.id] ?? desc.default_val;
      const displayVal = liveParamValues[desc.id] ?? val;
      const sMin = desc.min;
      const sMax = desc.type === LED_PARAM_TYPES.HUE ? 255 : desc.max;
      rendered.push(
        <div key={desc.id} className="grid gap-1">
          <div className="flex items-center justify-between">
            <Label className="text-sm">{label}</Label>
            <span className="text-xs font-mono tabular-nums text-muted-foreground">{displayVal}</span>
          </div>
          <CommitSlider
            min={sMin}
            max={sMax}
            step={desc.step || 1}
            value={displayVal}
            hideValue
            onLiveChange={(v) => {
              setLiveParamValues((prev) => {
                if (prev[desc.id] === v) {
                  return prev;
                }
                return { ...prev, [desc.id]: v };
              });
              liveEffectParamUpdates({ [desc.id]: v });
            }}
            onCommit={(v) => {
              void liveParams.cancelAndWait().then(() => {
                setLiveParamValues((prev) => {
                  if (!(desc.id in prev)) {
                    return prev;
                  }
                  const next = { ...prev };
                  delete next[desc.id];
                  return next;
                });
                commitEffectParamUpdates({ [desc.id]: v });
              });
            }}
            disabled={!connected || !paramsLoaded}
          />
        </div>,
      );
      i++;
    }

    return <div className="flex flex-col gap-4">{rendered}</div>;
  }

  const previewKeyColorMap = useMemo(() => {
    if (!pixelColors) {
      return undefined;
    }

    const map: Record<string, string> = {};
    for (let i = 0; i < KEY_COUNT; i++) {
      const c = pixelColors[i];
      if (!c) {
        continue;
      }
      map[`key-${i}`] = rgbToHex(c.r, c.g, c.b);
    }
    return map;
  }, [pixelColors]);

  const keyboardPreview = pixelColors ? (
    <MatrixKeyboard
      pixelColors={pixelColors}
      connected={connected && isMatrixMode}
      onPaint={paintMatrixKey}
      onStrokeEnd={handleMatrixStrokeEnd}
    />
  ) : (
    <BaseKeyboard
      mode="single"
      onButtonClick={() => { }}
      showLayerSelector={false}
      showRotary={false}
      interactive={false}
      showTooltips={false}
      keyColorMap={previewKeyColorMap}
      loading={connected && allPixelsQ.isLoading}
    />
  );

  const matrixToolsVisible = connected && isMatrixMode;


  const menubar = (
    <Toolbar
      left={
        matrixToolsVisible ? (
          <>
            <ToolbarStat value="Matrix mode" tone="active" />
            <span className="truncate text-xs text-muted-foreground">
              Paint directly on the keyboard above
            </span>
          </>
        ) : (
          <>
            <ToolbarStat label="Effect" value={effectName(currentEffect)} />
            <span className="truncate text-xs text-muted-foreground">
              {liveKeyboardPreviewEnabled ? "Live preview" : "Static preview"} — read only
            </span>
          </>
        )
      }
      right={
        <>
          {matrixToolsVisible && (
            <>
              <ColorPicker color={paintColor} onChange={setPaintColor} />
              <Button variant="outline" size="sm" onClick={() => fillMut.mutate(paintColor)}>
                <IconBrush className="size-3.5" />
                Fill
              </Button>
              <Button variant="outline" size="sm" onClick={() => clearMut.mutate()}>
                <IconTrash className="size-3.5" />
                Clear
              </Button>
            </>
          )}
          <AutosaveStatus state={saveState} />
        </>
      }
    />
  );

  return (
    <KeyboardEditor
      keyboard={keyboardPreview}
      menubar={menubar}
    >
      <div className="grid gap-5 xl:grid-cols-[minmax(0,1fr)_340px]">
        {/* Left column — what the lighting does */}
        <div className="flex flex-col gap-5">
          <PageSection title="Effect">
            <SectionCard
              title="Effect mode"
              description="Pick what the RGB matrix renders when no override is active."
              icon={<IconSparkles />}
              headerRight={
                <Badge variant="secondary" className="max-w-40 truncate">
                  {effectName(currentEffect)}
                </Badge>
              }
            >
              <div className="mb-4">
                <div className="relative">
                  <IconSearch className="pointer-events-none absolute left-3 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
                  <Input
                    value={effectSearch}
                    onChange={(event) => setEffectSearch(event.target.value)}
                    placeholder="Search effects…"
                    className="h-8 pl-8"
                  />
                </div>
              </div>

              {groupedEffects.length === 0 ? (
                <EmptyState
                  size="sm"
                  icon={<IconSearch />}
                  title="No effect matches your search"
                  description="Try a different term, or clear the box to see everything."
                />
              ) : (
                <div className="flex flex-col gap-4">
                  {groupedEffects.map((category) => (
                    <div key={category.key}>
                      <div className="mb-2 flex items-center gap-2">
                        <p className="text-[0.68rem] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
                          {category.title}
                        </p>
                        <div className="h-px flex-1 bg-border" />
                      </div>
                      <div className="grid grid-cols-2 gap-2 sm:grid-cols-3">
                        {category.effects.map((eff) => {
                          const active = Number(currentEffectForSelection) === eff.id;
                          return (
                            <button
                              key={eff.id}
                              type="button"
                              role="radio"
                              aria-checked={active}
                              disabled={!connected || effectQ.data == null}
                              onClick={() => effectMut.mutate(eff.id)}
                              className={cn(
                                "flex items-center gap-2 rounded-lg border px-2.5 py-2 text-left text-xs font-medium transition-colors",
                                "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60",
                                "disabled:pointer-events-none disabled:opacity-45",
                                active
                                  ? "border-primary/50 bg-primary/10 text-foreground ring-1 ring-primary/25"
                                  : "border-border bg-card text-muted-foreground hover:bg-muted hover:text-foreground",
                              )}
                            >
                              <span
                                className={cn(
                                  "size-1.5 shrink-0 rounded-full",
                                  active ? "bg-primary" : "bg-muted-foreground/30",
                                )}
                              />
                              <span className="truncate">{eff.name}</span>
                            </button>
                          );
                        })}
                      </div>
                    </div>
                  ))}
                </div>
              )}

              {currentEffect === LEDEffect.AUDIO_SPECTRUM && (
                <div
                  className={cn(
                    "mt-4 flex items-start gap-2 rounded-lg border px-3 py-2 text-xs",
                    connected && pageVisible
                      ? "border-success/30 bg-success/8 text-success"
                      : "border-border bg-muted/50 text-muted-foreground",
                  )}
                >
                  <IconMusic className="mt-px size-3.5 shrink-0" />
                  <span>
                    {connected && pageVisible
                      ? "Streaming PC audio FFT bands to the keyboard in real time."
                      : "Connect a keyboard and keep this page open to stream audio FFT data."}
                  </span>
                </div>
              )}
            </SectionCard>
          </PageSection>

          <PageSection title="Output">
            <SectionCard
              title="LED output"
              description="Master power and how hard the matrix is driven."
              icon={<IconBulb />}
              headerRight={
                <Switch
                  checked={enabledQ.data ?? false}
                  disabled={!connected}
                  onCheckedChange={(v) => enabledMut.mutate(v)}
                  aria-label="Enable LEDs"
                />
              }
            >
              <div className="flex flex-col gap-5">
                <SliderField
                  label="Global brightness"
                  description="Applies on top of every effect."
                  min={0} max={255} step={1}
                  value={brightnessQ.data ?? 128}
                  onLiveChange={(v) => liveBrightness(v)}
                  onCommit={(v) => {
                    void liveBrightness.cancelAndWait().then(() => brightnessMut.mutate(v));
                  }}
                  disabled={!connected || brightnessQ.data == null}
                />
                <SliderField
                  label="Frame rate limit"
                  description="Caps how often the matrix redraws. 0 leaves it uncapped."
                  min={0} max={120} step={1}
                  unit="fps"
                  valueFormatter={(v) => (v === 0 ? "Uncapped" : String(v))}
                  value={fpsQ.data ?? 0}
                  onLiveChange={(v) => liveFps(v)}
                  onCommit={(v) => {
                    void liveFps.cancelAndWait().then(() => fpsMut.mutate(v));
                  }}
                  disabled={!connected || fpsQ.data == null}
                />
              </div>
            </SectionCard>
          </PageSection>

          <PageSection title="Power &amp; idle">
            <SectionCard
              title="Idle behaviour"
              description="What happens to the RGB when the keyboard is left alone."
              icon={<IconMoon />}
            >
              <div className="flex flex-col gap-5">
                <SliderField
                  label="Auto-off after inactivity"
                  description="Turn the matrix off once the keyboard has been idle this long."
                  min={0} max={255} step={1}
                  unit="s"
                  valueFormatter={(v) => (v === 0 ? "Never" : String(v))}
                  value={ledIdleTimeoutSeconds}
                  onCommit={(v) => commitIdleOptions({ idle_timeout_seconds: v })}
                  disabled={!connected || idleOptionsQ.data == null}
                />

                <FormRows>
                  <FormRow
                    label="Keep system indicators"
                    description="Caps Lock and volume LEDs stay available while the matrix is off."
                  >
                    <Switch
                      checked={allowSystemWhenDisabled}
                      disabled={!connected || idleOptionsQ.data == null}
                      onCheckedChange={(v) => commitIdleOptions({ allow_system_when_disabled: v })}
                    />
                  </FormRow>
                  <FormRow
                    label="External writes count as activity"
                    description="Third-party RGB software keeps the matrix awake."
                  >
                    <Switch
                      checked={thirdPartyStreamCountsAsActivity}
                      disabled={!connected || idleOptionsQ.data == null}
                      onCheckedChange={(v) =>
                        commitIdleOptions({ third_party_stream_counts_as_activity: v })
                      }
                    />
                  </FormRow>
                  <FormRow
                    label="Off on USB suspend"
                    description="Cut the matrix when the host sleeps but keeps USB powered."
                  >
                    <Switch
                      checked={usbSuspendRgbOff}
                      disabled={!connected || idleOptionsQ.data == null}
                      onCheckedChange={(v) => commitIdleOptions({ usb_suspend_rgb_off: v })}
                    />
                  </FormRow>
                </FormRows>
              </div>
            </SectionCard>
          </PageSection>

          {isMatrixMode && (
            <PageSection title="Matrix">
              <SectionCard
                title="Transfer"
                description="Move the painted matrix between the keyboard and a .led file."
                icon={<IconArrowsExchange />}
              >
                <div className="flex flex-wrap gap-2">
                  <Button
                    variant="outline"
                    size="sm"
                    disabled={!connected || !pixelColors}
                    onClick={() => {
                      if (pixelColors) {
                        uploadMut.mutate(rgbArrayToPixels(pixelColors));
                      }
                    }}
                  >
                    <IconUpload className="size-3.5" />
                    Upload to keyboard
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    disabled={!connected}
                    onClick={() => downloadMut.mutate()}
                  >
                    <IconDownload className="size-3.5" />
                    Download from keyboard
                  </Button>
                  <div className="mx-1 w-px self-stretch bg-border" />
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={handleExport}
                    disabled={!connected || (!matrixPixels && !allPixelsQ.data)}
                  >
                    <IconFileExport className="size-3.5" />
                    Export .led
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={() => fileInputRef.current?.click()}
                    disabled={!connected}
                  >
                    <IconFileImport className="size-3.5" />
                    Import .led
                  </Button>
                  <input
                    ref={fileInputRef}
                    type="file"
                    accept=".led"
                    className="hidden"
                    aria-label="Import .led file"
                    onChange={handleImport}
                  />
                </div>
              </SectionCard>
            </PageSection>
          )}
        </div>

        {/* Right column — tuning for the selected effect, kept in view */}
        <div className="flex h-fit flex-col gap-4 self-start xl:sticky xl:top-0">
          <SectionCard
            title="Effect tuning"
            description={`Parameters for ${effectName(currentEffect)}.`}
            icon={<IconAdjustments />}
            footer={
              <Button
                variant="destructive"
                size="sm"
                onClick={handleResetEffectParams}
                disabled={!canResetEffectParams}
              >
                <IconRestore className="size-4" />
                Reset to defaults
              </Button>
            }
          >
            {renderEffectParams()}
          </SectionCard>

          {!isMatrixMode && (
            <SectionCard
              title="Preview"
              description="Only affects this window, never the keyboard."
              icon={<IconEye />}
            >
              <FormRows>
                <FormRow
                  label="Live preview"
                  description="Mirror the running effect in the keyboard above."
                >
                  <Switch
                    checked={liveKeyboardPreviewEnabled}
                    onCheckedChange={setLiveKeyboardPreviewEnabled}
                  />
                </FormRow>
              </FormRows>
            </SectionCard>
          )}
        </div>
      </div>
    </KeyboardEditor>
  );
}


export default function Lighting() {
  const status = useDeviceSession((state) => state.status);
  const [selectedBridgeSerialNumber, setSelectedBridgeSerialNumber] = useState<string | null>(null);
  const bridgeDevicesQ = useQuery({
    queryKey: ["libhmk-rgb-bridge", "devices"],
    queryFn: () => libhmkRgbBridge.listDevices(),
    enabled: isTauri() && status !== "connected" && status !== "updater",
    retry: 1,
    staleTime: 1_000,
    refetchInterval: 3_000,
  });
  const bridgeDevices = useMemo(() => bridgeDevicesQ.data ?? [], [bridgeDevicesQ.data]);
  const bridgeSelection = useMemo(() => {
    try {
      return {
        device: selectRgbBridgeDevice(bridgeDevices, selectedBridgeSerialNumber),
        error: null,
      };
    } catch (error) {
      return {
        device: null,
        error: error instanceof Error ? error.message : String(error),
      };
    }
  }, [bridgeDevices, selectedBridgeSerialNumber]);

  /* Once an unambiguous device has been observed, pin its serial so a later
   * path change or replacement device can never silently switch the target. */
  useEffect(() => {
    if (selectedBridgeSerialNumber || bridgeDevices.length !== 1) return;
    try {
      setSelectedBridgeSerialNumber(rgbBridgeSerialNumber(bridgeDevices[0]!));
    } catch {
      // The validation error is rendered below.
    }
  }, [bridgeDevices, selectedBridgeSerialNumber]);

  if (bridgeDevices.length > 0) {
    if (bridgeSelection.error) {
      return (
        <div className="flex flex-1 items-center justify-center p-6">
          <SectionCard title="libhmk RGB target rejected" description={bridgeSelection.error}>
            <p className="text-sm text-muted-foreground">
              Reconnect the intended keyboard and make sure every attached libhmk device exposes a unique USB serial number.
            </p>
          </SectionCard>
        </div>
      );
    }

    if (!bridgeSelection.device) {
      return (
        <div className="flex flex-1 items-center justify-center p-6">
          <SectionCard
            title="Select a libhmk keyboard"
            description="Multiple RGB bridges are attached. Choose the physical keyboard by its stable USB serial before any command is sent."
          >
            <Select
              value={selectedBridgeSerialNumber ?? undefined}
              items={bridgeDevices.map((device) => ({
                value: rgbBridgeSerialNumber(device),
                label: `${device.product?.trim() || "KBHE 75HE (libhmk)"} · ${rgbBridgeSerialNumber(device)}`,
              }))}
              onValueChange={setSelectedBridgeSerialNumber}
            >
              <SelectTrigger className="w-full min-w-72">
                <SelectValue placeholder="Choose a keyboard…" />
              </SelectTrigger>
              <SelectContent>
                {bridgeDevices.map((device) => {
                  const serialNumber = rgbBridgeSerialNumber(device);
                  return (
                    <SelectItem key={serialNumber} value={serialNumber}>
                      {device.product?.trim() || "KBHE 75HE (libhmk)"} · {serialNumber}
                    </SelectItem>
                  );
                })}
              </SelectContent>
            </Select>
          </SectionCard>
        </div>
      );
    }

    const bridgeDevice = bridgeSelection.device;
    return (
      <LibhmkLighting
        key={`${rgbBridgeSerialNumber(bridgeDevice)}:${bridgeDevice.path}`}
        device={bridgeDevice}
      />
    );
  }

  return <NativeLighting />;
}
