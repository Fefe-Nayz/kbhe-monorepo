import { LAYER_NAMES } from "@/constants/defaultLayout";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { selectItemsReverse } from "@/lib/utils";

interface LayerSelectProps {
  value: number;
  onChange: (layer: number) => void;
  className?: string;
}

const layerItems = selectItemsReverse(LAYER_NAMES);

export function LayerSelect({ value, onChange, className }: LayerSelectProps) {
  return (
    <Select value={String(value)} items={layerItems} onValueChange={(v) => onChange(Number(v))}>
      <SelectTrigger className={className ?? "w-40 h-8 text-sm"}>
        <SelectValue />
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          {Object.entries(LAYER_NAMES).map(([idx, name]) => (
            <SelectItem key={idx} value={idx}>
              {name}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </Select>
  );
}
