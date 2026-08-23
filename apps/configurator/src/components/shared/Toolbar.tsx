import { type ReactNode } from "react";
import { cn } from "@/lib/utils";

/**
 * The control strip that sits under the keyboard preview on editor pages.
 * `left` holds context (layer, selection), `right` holds actions.
 */
export function Toolbar({
  left,
  right,
  className,
}: {
  left?: ReactNode;
  right?: ReactNode;
  className?: string;
}) {
  return (
    <div className={cn("flex w-full min-w-0 items-center justify-between gap-4", className)}>
      <div className="flex min-w-0 items-center gap-2">{left}</div>
      <div className="flex shrink-0 items-center gap-2">{right}</div>
    </div>
  );
}

/** Thin vertical rule between toolbar clusters. */
export function ToolbarDivider({ className }: { className?: string }) {
  return <div className={cn("h-5 w-px shrink-0 bg-border", className)} />;
}

/**
 * Read-only status chip for a toolbar — a label/value pair that reads as data,
 * not as a button. Used for "3 selected", "Base layer", "Device disconnected".
 */
export function ToolbarStat({
  label,
  value,
  tone = "default",
  className,
}: {
  label?: string;
  value: ReactNode;
  tone?: "default" | "muted" | "active" | "danger";
  className?: string;
}) {
  return (
    <span
      className={cn(
        "inline-flex h-7 shrink-0 items-center gap-1.5 rounded-md border px-2.5 text-xs",
        tone === "default" && "border-border bg-muted/50 text-foreground",
        tone === "muted" && "border-transparent bg-transparent text-muted-foreground",
        tone === "active" && "border-primary/30 bg-primary/10 text-primary",
        tone === "danger" && "border-destructive/30 bg-destructive/10 text-destructive",
        className,
      )}
    >
      {label && <span className="text-muted-foreground">{label}</span>}
      <span className="font-medium">{value}</span>
    </span>
  );
}
