import { useState, useCallback, useRef, useEffect, type ReactNode } from "react";
import { Slider } from "@/components/ui/slider";
import { Label } from "@/components/ui/label";
import { cn } from "@/lib/utils";

interface DistanceSliderProps {
  label: string;
  value: number;
  /** Fires on every drag tick — use for live device preview (runtime-only SET). */
  onLiveChange?: (value: number) => void;
  /** Fires once on pointer-up — the commit value. */
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  displayDecimals?: number;
  disabled?: boolean;
  className?: string;
  /** One line under the track explaining what the distance controls. */
  description?: ReactNode;
  /** Rendered beside the label — "Mixed" badges, hints. */
  labelRight?: ReactNode;
  /** Hide the min/max endpoints under the track. */
  hideRange?: boolean;
}

export function DistanceSlider({
  label,
  value,
  onLiveChange,
  onChange,
  min = 0.1,
  max = 4.0,
  step = 0.1,
  displayDecimals = 1,
  disabled = false,
  className,
  description,
  labelRight,
  hideRange = false,
}: DistanceSliderProps) {
  const [draft, setDraft] = useState<number | null>(null);
  const commitRef = useRef<number>(value);
  const waitingForRef = useRef<number | null>(null);
  const clearTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const displayValue = draft ?? value;
  const precision = Math.max(0, Math.min(3, Math.trunc(displayDecimals)));

  // Clear draft as soon as the value prop catches up to the committed value.
  useEffect(() => {
    if (waitingForRef.current === null) return;
    if (value === waitingForRef.current) {
      setDraft(null);
      waitingForRef.current = null;
      if (clearTimerRef.current) {
        clearTimeout(clearTimerRef.current);
        clearTimerRef.current = null;
      }
    }
  }, [value]);

  const handleChange = useCallback((v: number | readonly number[]) => {
    const n = typeof v === "number" ? v : (v as readonly number[])[0];
    if (n !== undefined) {
      setDraft(n);
      commitRef.current = n;
      onLiveChange?.(n);
    }
  }, [onLiveChange]);

  const handlePointerDown = useCallback(() => {
    setDraft(value);
    commitRef.current = value;
    waitingForRef.current = null;
    if (clearTimerRef.current) {
      clearTimeout(clearTimerRef.current);
      clearTimerRef.current = null;
    }
  }, [value]);

  const handlePointerUp = useCallback(() => {
    if (draft === null) return;
    const final = commitRef.current;
    // Keep draft alive until value prop matches — no flash.
    waitingForRef.current = final;
    // Fallback: always clear after 1.5 s (handles mutation errors / device rounding).
    if (clearTimerRef.current) clearTimeout(clearTimerRef.current);
    clearTimerRef.current = setTimeout(() => {
      setDraft(null);
      waitingForRef.current = null;
      clearTimerRef.current = null;
    }, 1500);
    onChange(final);
  }, [draft, onChange]);

  return (
    <div className={cn("grid gap-2", disabled && "opacity-55", className)}>
      <div className="flex items-baseline justify-between gap-3">
        <div className="flex min-w-0 items-center gap-2">
          <Label className="truncate text-sm font-medium">{label}</Label>
          {labelRight}
        </div>
        <span className="shrink-0 rounded-md bg-muted px-1.5 py-0.5 font-mono text-xs tabular-nums text-foreground">
          {displayValue.toFixed(precision)}
          <span className="ml-0.5 text-muted-foreground">mm</span>
        </span>
      </div>
      <div
        onPointerDown={handlePointerDown}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onLostPointerCapture={handlePointerUp}
      >
        <Slider
          min={min}
          max={max}
          step={step}
          value={[displayValue]}
          onValueChange={handleChange}
          disabled={disabled}
          className="w-full"
        />
      </div>
      {(description || !hideRange) && (
        <div className="flex items-start justify-between gap-3">
          {description ? (
            <p className="text-xs leading-relaxed text-muted-foreground">{description}</p>
          ) : (
            <span />
          )}
          {!hideRange && (
            <span className="shrink-0 font-mono text-[0.68rem] tabular-nums text-muted-foreground/70">
              {min.toFixed(precision)}–{max.toFixed(precision)} mm
            </span>
          )}
        </div>
      )}
    </div>
  );
}
