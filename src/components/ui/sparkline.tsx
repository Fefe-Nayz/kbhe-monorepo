import { cn } from "@/lib/utils";

interface SparklineProps {
  values: number[];
  className?: string;
  colorClassName?: string;
  width?: number;
  height?: number;
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

export function Sparkline({
  values,
  className,
  colorClassName,
  width = 96,
  height = 24,
}: SparklineProps) {
  if (values.length < 2) {
    return <div className={cn("h-6 w-24 shrink-0", className)} aria-hidden />;
  }

  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min;

  const toY = (value: number) => {
    if (range === 0) {
      return height / 2;
    }
    return height - ((value - min) / range) * height;
  };

  const points = values
    .map((value, index) => {
      const x = (index / Math.max(1, values.length - 1)) * width;
      const y = clamp(toY(value), 0, height);
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");

  const lastValue = values[values.length - 1] ?? 0;
  const lastY = clamp(toY(lastValue), 0, height);

  return (
    <svg
      viewBox={`0 0 ${width} ${height}`}
      className={cn("h-6 w-24 shrink-0", colorClassName ?? "text-primary/70", className)}
      preserveAspectRatio="none"
      aria-hidden
    >
      <polyline
        points={points}
        fill="none"
        stroke="currentColor"
        strokeWidth={1.5}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <circle cx={width} cy={lastY} r={1.6} fill="currentColor" />
    </svg>
  );
}
