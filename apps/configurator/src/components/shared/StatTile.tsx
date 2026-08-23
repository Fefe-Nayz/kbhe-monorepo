import { type ReactNode } from "react";
import { cn } from "@/lib/utils";

type StatTone = "default" | "primary" | "success" | "warning" | "danger" | "info";

const TONE_ICON_CLASSES: Record<StatTone, string> = {
  default: "bg-muted text-muted-foreground",
  primary: "bg-primary/10 text-primary",
  success: "bg-success/12 text-success",
  warning: "bg-warning/15 text-warning",
  danger: "bg-destructive/10 text-destructive",
  info: "bg-info/12 text-info",
};

interface StatTileProps {
  label: string;
  value: ReactNode;
  icon?: ReactNode;
  /** Small text under the value — units, timestamps, secondary detail. */
  hint?: ReactNode;
  /** Rendered at the right edge — sparkline, badge, action. */
  trailing?: ReactNode;
  tone?: StatTone;
  mono?: boolean;
  className?: string;
}

/** Compact metric tile — the single readout treatment for dashboards and headers. */
export function StatTile({
  label,
  value,
  icon,
  hint,
  trailing,
  tone = "default",
  mono,
  className,
}: StatTileProps) {
  return (
    <div
      className={cn(
        "flex items-center gap-3 rounded-xl border bg-card px-3.5 py-3",
        className,
      )}
    >
      {icon && (
        <div
          className={cn(
            "flex size-8 shrink-0 items-center justify-center rounded-lg [&_svg]:size-4",
            TONE_ICON_CLASSES[tone],
          )}
        >
          {icon}
        </div>
      )}
      <div className="min-w-0 flex-1">
        <p className="truncate text-[0.7rem] font-medium uppercase tracking-[0.06em] text-muted-foreground">
          {label}
        </p>
        <div
          className={cn(
            "mt-0.5 truncate text-sm font-semibold text-foreground",
            mono && "font-mono",
          )}
        >
          {value}
        </div>
        {hint && <p className="mt-0.5 truncate text-[0.7rem] text-muted-foreground">{hint}</p>}
      </div>
      {trailing && <div className="shrink-0">{trailing}</div>}
    </div>
  );
}
