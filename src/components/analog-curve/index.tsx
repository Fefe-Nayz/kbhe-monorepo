import { useCallback, useRef, useState } from "react";
import { Button } from "@/components/ui/button";
import { Tooltip, TooltipContent, TooltipTrigger, TooltipProvider } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";

const VIEW_W = 350;
const VIEW_H = 200;
const MAX_DISTANCE = 4.0;

export interface CurvePoint {
  x: number;
  y: number;
}

const PRESETS: Record<string, CurvePoint[]> = {
  Linear:     [{ x: 0, y: 0 }, { x: 117, y: 67 }, { x: 233, y: 133 }, { x: 350, y: 200 }],
  Aggressive: [{ x: 0, y: 0 }, { x: 50, y: 100 }, { x: 100, y: 175 }, { x: 350, y: 200 }],
  Slow:       [{ x: 0, y: 0 }, { x: 200, y: 25 }, { x: 300, y: 75 }, { x: 350, y: 200 }],
  Smooth:     [{ x: 0, y: 0 }, { x: 100, y: 20 }, { x: 250, y: 180 }, { x: 350, y: 200 }],
  Step:       [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 100, y: 200 }, { x: 350, y: 200 }],
  Instant:    [{ x: 0, y: 0 }, { x: 0, y: 200 }, { x: 175, y: 200 }, { x: 350, y: 200 }],
  Digital:    [{ x: 0, y: 0 }, { x: 175, y: 0 }, { x: 175, y: 200 }, { x: 350, y: 200 }],
};

interface AnalogCurveEditorProps {
  points: CurvePoint[];
  onChange: (points: CurvePoint[]) => void;
  className?: string;
}

function pointToDevice(p: CurvePoint): { distance: string; output: number } {
  return {
    distance: ((p.x / VIEW_W) * MAX_DISTANCE).toFixed(2),
    output: Math.round((1 - p.y / VIEW_H) * 255),
  };
}

export function AnalogCurveEditor({ points, onChange, className }: AnalogCurveEditorProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState<number | null>(null);
  const [localPoints, setLocalPoints] = useState<CurvePoint[]>(points);

  // Sync from parent when not dragging
  if (dragging === null && points !== localPoints && JSON.stringify(points) !== JSON.stringify(localPoints)) {
    setLocalPoints(points);
  }

  const handlePointerDown = useCallback((idx: number, e: React.PointerEvent) => {
    e.preventDefault();
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
    setDragging(idx);
  }, []);

  const handlePointerMove = useCallback(
    (e: React.PointerEvent) => {
      if (dragging === null || !containerRef.current) return;
      const rect = containerRef.current.getBoundingClientRect();
      const x = Math.max(0, Math.min(VIEW_W, ((e.clientX - rect.left) / rect.width) * VIEW_W));
      const y = Math.max(0, Math.min(VIEW_H, ((e.clientY - rect.top) / rect.height) * VIEW_H));

      setLocalPoints(prev => {
        const next = [...prev];
        const minX = dragging > 0 ? next[dragging - 1].x : 0;
        const maxX = dragging < next.length - 1 ? next[dragging + 1].x : VIEW_W;
        next[dragging] = { x: Math.max(minX, Math.min(maxX, x)), y };
        return next;
      });
    },
    [dragging],
  );

  const handlePointerUp = useCallback(() => {
    if (dragging !== null) {
      onChange(localPoints);
    }
    setDragging(null);
  }, [dragging, localPoints, onChange]);

  const polyPoints = localPoints.map((p) => `${p.x},${p.y}`).join(" ");
  const fillPoints = `0,${VIEW_H} ${polyPoints} ${VIEW_W},${VIEW_H}`;

  const deadzone1Width = localPoints[0]?.x ?? 0;
  const deadzone2Pct = ((VIEW_W - (localPoints[localPoints.length - 1]?.x ?? VIEW_W)) / VIEW_W) * 100;
  const deadzone1Pct = (deadzone1Width / VIEW_W) * 100;

  return (
    <TooltipProvider delay={0}>
      <div className={cn("flex flex-col gap-4", className)}>
        <div className="relative pl-8">
          <div
            ref={containerRef}
            className="relative rounded-md border bg-card overflow-hidden"
            style={{ aspectRatio: `${VIEW_W} / ${VIEW_H}` }}
            onPointerMove={handlePointerMove}
            onPointerUp={handlePointerUp}
          >
            <svg
              viewBox={`0 0 ${VIEW_W} ${VIEW_H}`}
              className="absolute inset-0 size-full"
              preserveAspectRatio="none"
            >
              <polygon points={fillPoints} className="fill-primary/15" />
              <polyline
                points={polyPoints}
                className="fill-none stroke-primary"
                strokeWidth={2}
                vectorEffect="non-scaling-stroke"
              />
            </svg>

            {deadzone1Pct > 1 && (
              <div
                className="absolute inset-y-0 left-0 bg-muted/50 flex items-end justify-center border-r border-dashed border-muted-foreground/20"
                style={{ width: `${deadzone1Pct}%` }}
              >
                <span className="mb-1 text-[9px] text-muted-foreground">DZ</span>
              </div>
            )}
            {deadzone2Pct > 1 && (
              <div
                className="absolute inset-y-0 right-0 bg-muted/50 flex items-end justify-center border-l border-dashed border-muted-foreground/20"
                style={{ width: `${deadzone2Pct}%` }}
              >
                <span className="mb-1 text-[9px] text-muted-foreground">DZ</span>
              </div>
            )}

            {localPoints.map((p, i) => {
              const info = pointToDevice(p);
              const leftPct = (p.x / VIEW_W) * 100;
              const topPct = (p.y / VIEW_H) * 100;
              return (
                <Tooltip key={i}>
                  <TooltipTrigger
                    render={
                      <div
                        className="absolute size-3.5 -translate-x-1/2 -translate-y-1/2 cursor-grab rounded-full border-2 border-primary bg-background shadow-sm active:cursor-grabbing hover:scale-125 transition-transform"
                        style={{ left: `${leftPct}%`, top: `${topPct}%` }}
                        onPointerDown={(e) => handlePointerDown(i, e)}
                      />
                    }
                  />
                  <TooltipContent side="top" sideOffset={8}>
                    {info.distance} mm → {info.output}
                  </TooltipContent>
                </Tooltip>
              );
            })}
          </div>

          <div className="mt-1 flex justify-between text-[10px] text-muted-foreground">
            <span>0 mm</span>
            <span>{MAX_DISTANCE} mm</span>
          </div>
          <div className="absolute left-0 top-0 flex flex-col justify-between text-[10px] text-muted-foreground" style={{ height: "calc(100% - 1.25rem)" }}>
            <span>255</span>
            <span>0</span>
          </div>
        </div>

        <div className="flex flex-wrap gap-1.5">
          {Object.entries(PRESETS).map(([name, pts]) => (
            <Button
              key={name}
              variant="outline"
              size="sm"
              className="h-7 text-xs"
              onClick={() => onChange(pts)}
            >
              {name}
            </Button>
          ))}
        </div>
      </div>
    </TooltipProvider>
  );
}
