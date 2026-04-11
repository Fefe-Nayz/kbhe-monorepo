import { useState, useCallback, useRef } from "react";
import { Slider } from "@/components/ui/slider";
import { Label } from "@/components/ui/label";
import { cn } from "@/lib/utils";

interface DistanceSliderProps {
  label: string;
  value: number;
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  disabled?: boolean;
  className?: string;
}

export function DistanceSlider({
  label,
  value,
  onChange,
  min = 0.1,
  max = 4.0,
  step = 0.1,
  disabled = false,
  className,
}: DistanceSliderProps) {
  const [draft, setDraft] = useState<number | null>(null);
  const commitRef = useRef<number>(value);

  const displayValue = draft ?? value;
  const displayMm = Math.round(displayValue * 10) / 10;

  const handleChange = useCallback((v: number | readonly number[]) => {
    const n = typeof v === "number" ? v : (v as readonly number[])[0];
    if (n !== undefined) {
      setDraft(n);
      commitRef.current = n;
    }
  }, []);

  const handlePointerDown = useCallback(() => {
    setDraft(value);
    commitRef.current = value;
  }, [value]);

  const handlePointerUp = useCallback(() => {
    if (draft === null) return;
    const final = commitRef.current;
    setDraft(null);
    onChange(final);
  }, [draft, onChange]);

  return (
    <div className={cn("grid gap-2", className)}>
      <div className="flex items-center justify-between">
        <Label className="text-sm font-medium">{label}</Label>
        <span className="text-sm font-mono tabular-nums text-muted-foreground">
          {displayMm.toFixed(1)} mm
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
    </div>
  );
}
