import { useEffect, useLayoutEffect, useCallback, useRef, useState, useMemo } from "react";

import {
  keyboardPreviewBaseLayout,
  LAYER_NAMES,
  previewKeyMetaById,
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
  renderKeyOverlay?: (keyId: string) => React.ReactNode;
  keyColorMap?: Record<string, string>;
}

function resolveLabel(label: string): React.ReactNode {
  return labelRegistry[label] ?? label;
}

const FRAME_PADDING = 18;
const FRAME_BORDER = 1;
const BASE_GAP = 5;

function computeUnit(containerWidth: number, boardWidth: number): number {
  const available = containerWidth - 32;
  if (available <= 0 || boardWidth <= 0) return 50;
  const ideal = (available - FRAME_PADDING * 2 - FRAME_BORDER * 2) / boardWidth;
  return Math.max(20, Math.min(70, ideal));
}

function useAutoUnit(
  containerRef: React.RefObject<HTMLDivElement | null>,
  layoutBounds: { width: number; height: number },
) {
  const [unit, setUnit] = useState<number | null>(null);

  useLayoutEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    setUnit(computeUnit(el.clientWidth, layoutBounds.width));
  }, [containerRef, layoutBounds.width]);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const ro = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) return;
      setUnit(computeUnit(entry.contentRect.width, layoutBounds.width));
    });

    ro.observe(el);
    return () => ro.disconnect();
  }, [containerRef, layoutBounds.width]);

  return unit;
}

export default function BaseKeyboard({
  mode = "single",
  onButtonClick,
  showLayerSelector = true,
  showRotary = true,
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
  const unit = useAutoUnit(containerRef, bounds);

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

  const ready = unit != null;

  return (
    <div
      ref={containerRef}
      className="relative w-full rounded-lg border bg-card p-4"
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
          <KeyboardLayout
            layout={keyboardPreviewBaseLayout}
            theme="modern"
            interactive
            unit={unit}
            gap={BASE_GAP}
            selectedKeyIds={keyboardSelectedIds}
            onKeyClick={(key) => handleSelection(key.id)}
            keyColorMap={keyColorMap}
            renderLegend={({ key, index }) => {
              if (renderKeyOverlay) {
                const overlay = renderKeyOverlay(key.id);
                if (overlay !== undefined) return overlay;
              }
              if (index !== 0) return null;
              const meta = previewKeyMetaById[key.id];
              const configuredLabel = layout.bindings[key.id]?.label[0];
              const layerDefault =
                currentLayer === 1 ? meta?.fnLabel || meta?.baseLabel || "" : meta?.baseLabel || "";
              return resolveLabel(configuredLabel || layerDefault);
            }}
          />

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
