import { type ReactNode } from "react";
import { cn } from "@/lib/utils";

interface EmptyStateProps {
  icon?: ReactNode;
  title: string;
  description?: ReactNode;
  action?: ReactNode;
  /** `card` draws a dashed container; `plain` renders bare (already inside a card). */
  variant?: "card" | "plain";
  size?: "sm" | "md" | "lg";
  className?: string;
}

const SIZE_CLASSES = {
  sm: "gap-2 px-5 py-7",
  md: "gap-3 px-6 py-10",
  lg: "gap-3.5 px-6 py-14",
} as const;

const ICON_SIZE_CLASSES = {
  sm: "size-9 [&_svg]:size-4",
  md: "size-11 [&_svg]:size-5",
  lg: "size-14 [&_svg]:size-6",
} as const;

/**
 * The single empty/placeholder treatment used everywhere: a haloed icon,
 * one line of intent, one line of guidance, and an optional next step.
 */
export function EmptyState({
  icon,
  title,
  description,
  action,
  variant = "card",
  size = "md",
  className,
}: EmptyStateProps) {
  return (
    <div
      className={cn(
        "flex flex-col items-center justify-center text-center",
        SIZE_CLASSES[size],
        variant === "card" &&
          "rounded-xl border border-dashed border-border bg-surface-sunken/60",
        className,
      )}
    >
      {icon && (
        <div
          className={cn(
            "flex items-center justify-center rounded-full bg-muted text-muted-foreground",
            ICON_SIZE_CLASSES[size],
          )}
        >
          {icon}
        </div>
      )}
      <div className="max-w-sm space-y-1">
        <p className="text-sm font-medium text-foreground">{title}</p>
        {description && (
          <p className="text-xs leading-relaxed text-muted-foreground">{description}</p>
        )}
      </div>
      {action && <div className="mt-1 flex items-center gap-2">{action}</div>}
    </div>
  );
}
