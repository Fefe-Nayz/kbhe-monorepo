import { useEffect, useLayoutEffect, useCallback, useRef, useState, useMemo } from "react";

import {
  keyboardPreviewBaseLayout,
  LAYER_NAMES,
  previewKeys,
  type RotaryTargetId,
} from "@/constants/defaultLayout";
import { KeyboardLayout } from "@/lib/vendor/react-kle-modern";
import { ensureParsedKeyboardLayout } from "@/lib/vendor/react-kle-modern/parser";
import { getKeyboardBounds } from "@/lib/vendor/react-kle-modern/utils";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { labelRegistry } from "@/ui/labels/labelRegistry";
import { cn } from "@/lib/utils";

interface BaseKeyboardProps {
  mode: "single" | "multi";
  onButtonClick: (ids: string[] | string) => void;
  showLayerSelector?: boolean;
  showRotary?: boolean;
  keyLegendMap?: Record<string, React.ReactNode>;
  keyLegendSlotsMap?: Record<string, Array<React.ReactNode | undefined>>;
  keyLegendClassName?: string;
  renderKeyOverlay?: (keyId: string) => React.ReactNode;
  keyColorMap?: Record<string, string>;
}

function resolveLabel(label: string): React.ReactNode {
  const registered = labelRegistry[label];
  if (registered) return registered;

  const lines = label
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean);

  if (lines.length >= 2) {
    return (
      <span className="flex flex-col items-center leading-[1.05]">
        <span>{lines[0]}</span>
        <span>{lines.slice(1).join(" ")}</span>
      </span>
    );
  }

  return label;
}

const FRAME_PADDING = 18;
const FRAME_BORDER = 1;
const BASE_GAP = 5;
const BASE_UNIT = 56;
const ROOT_HORIZONTAL_PADDING = 32;

function computeUnit(contentWidth: number, boardWidth: number): number {
  if (contentWidth <= 0 || boardWidth <= 0) return 50;
  const ideal = (contentWidth - FRAME_PADDING * 2 - FRAME_BORDER * 2) / boardWidth;
  return Math.max(20, Math.min(70, ideal));
}

function getContentWidth(element: HTMLDivElement): number {
  return Math.max(0, element.clientWidth - ROOT_HORIZONTAL_PADDING);
}

function useAutoScale(
  containerRef: React.RefObject<HTMLDivElement | null>,
  layoutBounds: { width: number; height: number },
) {
  const [scale, setScale] = useState<number | null>(null);

  useLayoutEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    setScale(computeUnit(getContentWidth(el), layoutBounds.width) / BASE_UNIT);
  }, [containerRef, layoutBounds.width]);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    let frame = 0;
    let lastScale = computeUnit(getContentWidth(el), layoutBounds.width) / BASE_UNIT;

    const ro = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) return;

      const nextScale = computeUnit(entry.contentRect.width, layoutBounds.width) / BASE_UNIT;
      if (Math.abs(nextScale - lastScale) < 0.001) return;

      lastScale = nextScale;
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => setScale(nextScale));
    });

    ro.observe(el);
    return () => {
      cancelAnimationFrame(frame);
      ro.disconnect();
    };
  }, [containerRef, layoutBounds.width]);

  return scale;
}

export default function BaseKeyboard({
  mode = "single",
  onButtonClick,
  showLayerSelector = true,
  showRotary = true,
  keyLegendMap,
  keyLegendSlotsMap,
  keyLegendClassName,
  renderKeyOverlay,
  keyColorMap,
}: BaseKeyboardProps) {
  const selectedKeys = useKeyboardStore((state) => state.selectedKeys);
  const toggleKeySelection = useKeyboardStore((state) => state.toggleKeySelection);
  const setMode = useKeyboardStore((state) => state.setMode);
  const setCurrentLayer = useKeyboardStore((state) => state.setCurrentLayer);
  const layout = useKeyboardStore((state) => state.layout);
  const currentLayer = useKeyboardStore((state) => state.currentLayer);

  const containerRef = useRef<HTMLDivElement>(null);
  const [areaStart, setAreaStart] = useState<{ x: number; y: number } | null>(null);
  const [areaEnd, setAreaEnd] = useState<{ x: number; y: number } | null>(null);

  const parsed = useMemo(
    () => ensureParsedKeyboardLayout(keyboardPreviewBaseLayout),
    [],
  );
  const bounds = useMemo(() => getKeyboardBounds(parsed.keys), [parsed.keys]);
  const scale = useAutoScale(containerRef, bounds);
  const boardWidth = Math.max(0, bounds.width * BASE_UNIT);
  const boardHeight = Math.max(0, bounds.height * BASE_UNIT);
  const framedWidth = boardWidth + FRAME_PADDING * 2 + FRAME_BORDER * 2;
  const framedHeight = boardHeight + FRAME_PADDING * 2 + FRAME_BORDER * 2;

  const handleSelection = useCallback(
    (id: string) => {
      const alreadySelected = selectedKeys.includes(id);
      const nextSelection =
        mode === "single"
          ? alreadySelected ? [] : [id]
          : alreadySelected
            ? selectedKeys.filter((key) => key !== id)
            : [...selectedKeys, id];

      toggleKeySelection(id);
      onButtonClick(mode === "single" ? (nextSelection[0] ?? "") : nextSelection);
    },
    [mode, selectedKeys, toggleKeySelection, onButtonClick],
  );

  const handleKeyClick = useCallback(
    (key: { id: string }) => handleSelection(key.id),
    [handleSelection],
  );

  useEffect(() => {
    setMode(mode);
  }, [mode, setMode]);

  const handleMouseDown = useCallback(
    (e: React.MouseEvent) => {
      if (mode !== "multi") return;
      const rect = containerRef.current?.getBoundingClientRect();
      if (!rect) return;
      setAreaStart({ x: e.clientX - rect.left, y: e.clientY - rect.top });
      setAreaEnd(null);
    },
    [mode],
  );

  const handleMouseMove = useCallback(
    (e: React.MouseEvent) => {
      if (!areaStart || mode !== "multi") return;
      const rect = containerRef.current?.getBoundingClientRect();
      if (!rect) return;
      setAreaEnd({ x: e.clientX - rect.left, y: e.clientY - rect.top });
    },
    [areaStart, mode],
  );

  const handleMouseUp = useCallback(() => {
    setAreaStart(null);
    setAreaEnd(null);
  }, []);

  const rotaryLabels: Record<RotaryTargetId, string> = {
    "rotary.ccw": layout.rotaryBindings["rotary.ccw"]?.label[0] ?? "CCW",
    "rotary.press": layout.rotaryBindings["rotary.press"]?.label[0] ?? "Press",
    "rotary.cw": layout.rotaryBindings["rotary.cw"]?.label[0] ?? "CW",
  };

  const keyboardSelectedIds = selectedKeys.filter((key) => key.startsWith("key-"));
  const overlayLegendMap = useMemo(() => {
    if (keyLegendMap) return keyLegendMap;
    if (!renderKeyOverlay) return undefined;

    const next: Record<string, React.ReactNode> = {};
    for (const key of previewKeys) {
      const overlay = renderKeyOverlay(key.id);
      if (overlay !== undefined) {
        next[key.id] = overlay;
      }
    }
    return next;
  }, [keyLegendMap, renderKeyOverlay]);

  const resolvedKeyLegendMap = useMemo(() => {
    const next: Record<string, React.ReactNode> = {};

    for (const meta of previewKeys) {
      const customLegend = overlayLegendMap?.[meta.id];
      if (customLegend !== undefined) {
        next[meta.id] = customLegend;
        continue;
      }

      const configuredLabel = layout.bindings[meta.id]?.label[0];
      const layerDefault =
        currentLayer === 1 ? meta.fnLabel || meta.baseLabel || "" : meta.baseLabel || "";

      next[meta.id] = resolveLabel(configuredLabel || layerDefault);
    }

    return next;
  }, [currentLayer, layout.bindings, overlayLegendMap]);

  const resolvedKeyLegendClassNameMap = useMemo(() => {
    if (!keyLegendClassName) return undefined;

    const legendIds = new Set<string>([
      ...Object.keys(overlayLegendMap ?? {}),
      ...Object.keys(keyLegendSlotsMap ?? {}),
      ...Object.keys(keyLegendMap ?? {}),
    ]);

    if (legendIds.size === 0) return undefined;

    const next: Record<string, string> = {};
    for (const keyId of legendIds) {
      next[keyId] = keyLegendClassName;
    }
    return next;
  }, [keyLegendClassName, overlayLegendMap, keyLegendSlotsMap, keyLegendMap]);

  const resolvedKeyLegendColorMap = useMemo(() => {
    const next: Record<string, string | undefined> = {};

    for (const meta of previewKeys) {
      if (overlayLegendMap?.[meta.id] !== undefined) continue;

      const color = layout.bindings[meta.id]?.color;
      if (color) {
        next[meta.id] = color;
      }
    }

    return next;
  }, [layout.bindings, overlayLegendMap]);

  const resolvedKeyLegendFontSizeMap = useMemo(() => {
    const next: Record<string, number | undefined> = {};

    for (const meta of previewKeys) {
      if (overlayLegendMap?.[meta.id] !== undefined) continue;

      const fontSize = layout.bindings[meta.id]?.fontSize;
      if (fontSize != null) {
        next[meta.id] = fontSize;
      }
    }

    return next;
  }, [layout.bindings, overlayLegendMap]);

  const ready = scale != null;
  const appliedScale = scale ?? 1;

  return (
    <div
      ref={containerRef}
      className="relative w-full rounded-lg  p-4"
      onMouseDown={handleMouseDown}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
    >
      {showLayerSelector && ready && (
        <div className="mb-3 flex items-center gap-1.5">
          {Object.entries(LAYER_NAMES).map(([layer, name]) => {
            const layerIndex = Number(layer);
            return (
              <button
                key={layer}
                type="button"
                onClick={() => setCurrentLayer(layerIndex)}
                className={cn(
                  "rounded-md px-3 py-1 text-xs font-medium transition-colors",
                  currentLayer === layerIndex
                    ? "bg-primary text-primary-foreground"
                    : "bg-secondary text-secondary-foreground hover:bg-accent",
                )}
              >
                {name}
              </button>
            );
          })}
        </div>
      )}

      {ready && (
        <div className="flex items-start justify-center gap-6">
          <div
            className="shrink-0"
            style={{
              width: framedWidth * appliedScale,
              height: framedHeight * appliedScale,
            }}
          >
            <div
              style={{
                width: framedWidth,
                height: framedHeight,
                transform: `scale(${appliedScale})`,
                transformOrigin: "top left",
              }}
            >
              <KeyboardLayout
                layout={keyboardPreviewBaseLayout}
                theme="modern"
                interactive
                unit={BASE_UNIT}
                gap={BASE_GAP}
                selectedKeyIds={keyboardSelectedIds}
                onKeyClick={handleKeyClick}
                keyColorMap={keyColorMap}
                keyLegendMap={resolvedKeyLegendMap}
                keyLegendSlotsMap={keyLegendSlotsMap}
                keyLegendClassNameMap={resolvedKeyLegendClassNameMap}
                keyLegendColorMap={resolvedKeyLegendColorMap}
                keyLegendFontSizeMap={resolvedKeyLegendFontSizeMap}
              />
            </div>
          </div>

          {showRotary && (
            <div className="shrink-0">
              <RotaryPreview
                selectedKeys={selectedKeys}
                onSelect={handleSelection}
                labels={rotaryLabels}
              />
            </div>
          )}
        </div>
      )}

      {areaStart && areaEnd && (
        <div
          className="pointer-events-none absolute border border-primary/50 bg-primary/10 rounded-sm"
          style={{
            left: Math.min(areaStart.x, areaEnd.x),
            top: Math.min(areaStart.y, areaEnd.y),
            width: Math.abs(areaEnd.x - areaStart.x),
            height: Math.abs(areaEnd.y - areaStart.y),
          }}
        />
      )}
    </div>
  );
}

function RotaryPreview({
  selectedKeys,
  onSelect,
  labels,
}: {
  selectedKeys: string[];
  onSelect: (id: RotaryTargetId) => void;
  labels: Record<RotaryTargetId, string>;
}) {
  const btnBase = "rounded-full border text-xs font-medium transition-colors";
  const btnActive = "border-primary bg-primary text-primary-foreground";
  const btnInactive = "border-border bg-card text-card-foreground hover:border-primary/50";

  return (
    <div className="flex flex-col items-center justify-center gap-3 rounded-lg border bg-card px-5 py-4">
      <span className="text-[11px] font-semibold uppercase tracking-[0.2em] text-muted-foreground">
        Rotary
      </span>
      <div className="flex items-center gap-3">
        <button
          type="button"
          onClick={() => onSelect("rotary.ccw")}
          className={cn(btnBase, "px-3 py-2", selectedKeys.includes("rotary.ccw") ? btnActive : btnInactive)}
        >
          {labels["rotary.ccw"]}
        </button>
        <button
          type="button"
          onClick={() => onSelect("rotary.press")}
          className={cn(
            btnBase,
            "flex h-20 w-20 items-center justify-center text-center font-semibold",
            selectedKeys.includes("rotary.press")
              ? cn(btnActive, "shadow-[0_0_0_6px_oklch(var(--primary)/0.18)]")
              : btnInactive,
          )}
        >
          {labels["rotary.press"]}
        </button>
        <button
          type="button"
          onClick={() => onSelect("rotary.cw")}
          className={cn(btnBase, "px-3 py-2", selectedKeys.includes("rotary.cw") ? btnActive : btnInactive)}
        >
          {labels["rotary.cw"]}
        </button>
      </div>
    </div>
  );
}
