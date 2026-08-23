import { type ReactNode } from "react";
import { cn } from "@/lib/utils";

export interface SegmentedOption<T extends string | number> {
  value: T;
  label: ReactNode;
  icon?: ReactNode;
  disabled?: boolean;
  title?: string;
}

interface SegmentedControlProps<T extends string | number> {
  value: T;
  options: SegmentedOption<T>[];
  onChange: (value: T) => void;
  size?: "sm" | "md";
  disabled?: boolean;
  className?: string;
  /** Stretch segments to fill the container evenly. */
  fill?: boolean;
  "aria-label"?: string;
}

/**
 * Inline exclusive choice: a track with a highlighted active segment.
 * Preferred over a Select whenever there are 2–4 options that fit on one line —
 * it shows the alternatives instead of hiding them behind a click.
 */
export function SegmentedControl<T extends string | number>({
  value,
  options,
  onChange,
  size = "sm",
  disabled,
  className,
  fill,
  "aria-label": ariaLabel,
}: SegmentedControlProps<T>) {
  return (
    <div
      role="radiogroup"
      aria-label={ariaLabel}
      className={cn(
        "inline-flex items-center gap-0.5 rounded-lg border bg-surface-sunken p-0.5",
        fill && "flex w-full",
        className,
      )}
    >
      {options.map((option) => {
        const active = option.value === value;
        return (
          <button
            key={String(option.value)}
            type="button"
            role="radio"
            aria-checked={active}
            title={option.title}
            disabled={disabled || option.disabled}
            onClick={() => onChange(option.value)}
            className={cn(
              "inline-flex items-center justify-center gap-1.5 rounded-[calc(var(--radius)-3px)] font-medium whitespace-nowrap transition-colors",
              "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60",
              "disabled:pointer-events-none disabled:opacity-45",
              size === "sm" ? "h-6.5 px-2.5 text-xs" : "h-8 px-3 text-sm",
              fill && "flex-1",
              active
                ? "bg-background text-foreground shadow-xs ring-1 ring-border"
                : "text-muted-foreground hover:bg-background/50 hover:text-foreground",
              "[&_svg]:size-3.5 [&_svg]:shrink-0",
            )}
          >
            {option.icon}
            {option.label}
          </button>
        );
      })}
    </div>
  );
}
