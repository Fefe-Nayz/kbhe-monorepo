import { cn } from "@/lib/utils";

interface Props {
  label: string;
  /** Color variant */
  variant?: "green" | "yellow" | "red" | "blue" | "default";
  className?: string;
}

const VARIANT_CLASSES: Record<NonNullable<Props["variant"]>, string> = {
  green:   "bg-green-100 text-green-700 dark:bg-green-900/30 dark:text-green-400",
  yellow:  "bg-yellow-100 text-yellow-700 dark:bg-yellow-900/30 dark:text-yellow-400",
  red:     "bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400",
  blue:    "bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400",
  default: "bg-muted text-muted-foreground",
};

export function LiveStatusChip({ label, variant = "default", className }: Props) {
  return (
    <span
      className={cn(
        "inline-flex items-center gap-1.5 rounded-full px-2.5 py-0.5 text-xs font-medium",
        VARIANT_CLASSES[variant],
        className,
      )}
    >
      {(variant === "green" || variant === "blue") && (
        <span className="size-1.5 rounded-full bg-current animate-pulse" />
      )}
      {label}
    </span>
  );
}
