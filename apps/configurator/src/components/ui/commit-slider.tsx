import { useCallback, useEffect, useRef, useState } from "react";

import { cn } from "@/lib/utils";
import { Slider } from "@/components/ui/slider";

interface CommitSliderProps {
  value: number;
  onCommit: (value: number) => void;
  onLiveChange?: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  valueFormatter?: (value: number) => string;
  hideValue?: boolean;
  disabled?: boolean;
  className?: string;
}

function getStepPrecision(step: number): number {
  if (!Number.isFinite(step) || step <= 0) return 0;
  const stepText = String(step);
  if (!stepText.includes(".")) return 0;
  return stepText.split(".")[1]?.length ?? 0;
}

export function CommitSlider({
  value,
  onCommit,
  onLiveChange,
  min = 0,
  max = 100,
  step = 1,
  valueFormatter,
  hideValue = false,
  disabled = false,
  className,
}: CommitSliderProps) {
  const [draft, setDraft] = useState<number | null>(null);
  const commitRef = useRef<number>(value);
  const waitingForRef = useRef<number | null>(null);
  const clearTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const displayValue = draft ?? value;
  const precision = getStepPrecision(step);
  const displayText = valueFormatter
    ? valueFormatter(displayValue)
    : Number(displayValue).toFixed(precision);

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

  useEffect(() => {
    return () => {
      if (clearTimerRef.current) {
        clearTimeout(clearTimerRef.current);
      }
    };
  }, []);

  const handleValueChange = useCallback((next: number | readonly number[]) => {
    const n = typeof next === "number" ? next : next[0];
    if (n === undefined) return;
    setDraft(n);
    commitRef.current = n;
    onLiveChange?.(n);
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
    const finalValue = commitRef.current;
    waitingForRef.current = finalValue;

    if (clearTimerRef.current) {
      clearTimeout(clearTimerRef.current);
    }
    clearTimerRef.current = setTimeout(() => {
      setDraft(null);
      waitingForRef.current = null;
      clearTimerRef.current = null;
    }, 1500);

    onCommit(finalValue);
  }, [draft, onCommit]);

  return (
    <div
      className={cn("w-full", className)}
      onPointerDown={handlePointerDown}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      onLostPointerCapture={handlePointerUp}
    >
      {!hideValue && (
        <div className="mb-1 flex justify-end">
          <span className="text-xs font-mono tabular-nums text-muted-foreground">
            {displayText}
          </span>
        </div>
      )}
      <Slider
        min={min}
        max={max}
        step={step}
        value={[displayValue]}
        onValueChange={handleValueChange}
        disabled={disabled}
      />
    </div>
  );
}
