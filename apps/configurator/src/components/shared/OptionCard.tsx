import { type ReactNode } from "react";
import { cn } from "@/lib/utils";
import { IconCheck } from "@tabler/icons-react";

interface OptionCardProps {
  title: ReactNode;
  description?: ReactNode;
  icon?: ReactNode;
  /** Line under the description — current value, "Not configured", key count. */
  status?: ReactNode;
  selected?: boolean;
  disabled?: boolean;
  onClick?: () => void;
  className?: string;
  children?: ReactNode;
}

/**
 * A selectable tile. Used wherever the user picks one thing from a small
 * visual set (advanced-key behaviours, lighting effects, profile slots).
 * Selection is shown three ways — ring, tint, and a check — so it survives
 * both themes and colour-blind viewers.
 */
export function OptionCard({
  title,
  description,
  icon,
  status,
  selected,
  disabled,
  onClick,
  className,
  children,
}: OptionCardProps) {
  return (
    <button
      type="button"
      disabled={disabled}
      aria-pressed={selected}
      onClick={onClick}
      className={cn(
        "group relative flex w-full flex-col items-start gap-1.5 rounded-xl border p-3.5 text-left transition-all",
        "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60",
        "disabled:pointer-events-none disabled:opacity-45",
        selected
          ? "border-primary/45 bg-primary/8 ring-1 ring-primary/25"
          : "border-border bg-card hover:border-border hover:bg-muted/50",
        className,
      )}
    >
      {selected && (
        <span className="absolute right-2.5 top-2.5 flex size-4 items-center justify-center rounded-full bg-primary text-primary-foreground">
          <IconCheck className="size-2.5" stroke={3} />
        </span>
      )}
      <div className="flex w-full min-w-0 items-center gap-2 pr-5">
        {icon && (
          <span
            className={cn(
              "flex size-6 shrink-0 items-center justify-center rounded-md transition-colors [&_svg]:size-3.5",
              selected ? "bg-primary/15 text-primary" : "bg-muted text-muted-foreground",
            )}
          >
            {icon}
          </span>
        )}
        <span
          className={cn(
            "truncate text-sm font-medium",
            selected ? "text-primary" : "text-foreground",
          )}
        >
          {title}
        </span>
      </div>
      {description && (
        <span className="text-xs leading-relaxed text-muted-foreground">{description}</span>
      )}
      {status && (
        <span
          className={cn(
            "mt-0.5 text-[0.7rem] font-medium",
            selected ? "text-primary/80" : "text-muted-foreground/80",
          )}
        >
          {status}
        </span>
      )}
      {children}
    </button>
  );
}
