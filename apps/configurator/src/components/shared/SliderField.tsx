import { type ReactNode } from "react";
import { cn } from "@/lib/utils";
import { CommitSlider } from "@/components/ui/commit-slider";

interface SliderFieldProps {
  label: ReactNode;
  description?: ReactNode;
  value: number;
  onCommit: (value: number) => void;
  onLiveChange?: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  /** Appended to the readout, e.g. "ms", "Hz", "%". */
  unit?: string;
  valueFormatter?: (value: number) => string;
  /** Show the min/max endpoints under the track. */
  showRange?: boolean;
  disabled?: boolean;
  className?: string;
  /** Rendered next to the label — badges, reset buttons. */
  labelRight?: ReactNode;
}

/**
 * A slider that carries its own label, live readout and range.
 * The pages previously paired a bare `<span>` with a `CommitSlider`, which left
 * the value floating unlabelled above the track and the bounds invisible.
 */
export function SliderField({
  label,
  description,
  value,
  onCommit,
  onLiveChange,
  min = 0,
  max = 100,
  step = 1,
  unit,
  valueFormatter,
  showRange = true,
  disabled,
  className,
  labelRight,
}: SliderFieldProps) {
  const decimals = String(step).includes(".")
    ? (String(step).split(".")[1]?.length ?? 0)
    : 0;
  const plain = (v: number) => v.toFixed(decimals);

  // A custom formatter owns the whole string — some values read as words
  // ("Uncapped", "Never"), where appending a unit would be nonsense.
  const displayText = valueFormatter ? valueFormatter(value) : plain(value);
  const showUnit = Boolean(unit) && !valueFormatter;

  return (
    <div className={cn("flex flex-col gap-2", disabled && "opacity-55", className)}>
      <div className="flex items-baseline justify-between gap-3">
        <div className="flex min-w-0 items-center gap-2">
          <span className="truncate text-sm font-medium">{label}</span>
          {labelRight}
        </div>
        <span className="shrink-0 rounded-md bg-muted px-1.5 py-0.5 font-mono text-xs tabular-nums text-foreground">
          {displayText}
          {showUnit ? <span className="ml-0.5 text-muted-foreground">{unit}</span> : null}
        </span>
      </div>

      <CommitSlider
        value={value}
        onCommit={onCommit}
        onLiveChange={onLiveChange}
        min={min}
        max={max}
        step={step}
        hideValue
        disabled={disabled}
      />

      {(showRange || description) && (
        <div className="flex items-start justify-between gap-3">
          {description ? (
            <p className="text-xs leading-relaxed text-muted-foreground">{description}</p>
          ) : (
            <span />
          )}
          {showRange && (
            <span className="shrink-0 font-mono text-[0.68rem] tabular-nums text-muted-foreground/70">
              {plain(min)}–{plain(max)}
              {unit ? ` ${unit}` : ""}
            </span>
          )}
        </div>
      )}
    </div>
  );
}
