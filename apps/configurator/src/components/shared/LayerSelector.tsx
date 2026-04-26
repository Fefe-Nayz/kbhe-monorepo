import { LAYER_NAMES } from "@/lib/kbhe/protocol";
import { cn } from "@/lib/utils";

interface LayerSelectorProps {
  value: number;
  onChange: (layer: number) => void;
  className?: string;
}

export function LayerSelector({ value, onChange, className }: LayerSelectorProps) {
  return (
    <div className={cn("flex items-center gap-1", className)}>
      {Object.entries(LAYER_NAMES).map(([layer, name]) => {
        const idx = Number(layer);
        const active = idx === value;
        return (
          <button
            key={layer}
            type="button"
            onClick={() => onChange(idx)}
            className={cn(
              "rounded-full px-3 py-1 text-xs font-medium transition-colors",
              active
                ? "bg-primary text-primary-foreground"
                : "bg-muted text-muted-foreground hover:bg-muted/80",
            )}
          >
            {name}
          </button>
        );
      })}
    </div>
  );
}
