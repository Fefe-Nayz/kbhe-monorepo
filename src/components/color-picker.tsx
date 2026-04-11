import { useState, useCallback, useRef, useEffect } from "react";
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { cn } from "@/lib/utils";

export interface RGBColor {
  r: number;
  g: number;
  b: number;
}

const PRESET_COLORS: RGBColor[] = [
  { r: 255, g: 0, b: 0 },
  { r: 255, g: 127, b: 0 },
  { r: 255, g: 255, b: 0 },
  { r: 0, g: 255, b: 0 },
  { r: 0, g: 255, b: 255 },
  { r: 0, g: 127, b: 255 },
  { r: 0, g: 0, b: 255 },
  { r: 127, g: 0, b: 255 },
  { r: 255, g: 0, b: 255 },
  { r: 255, g: 255, b: 255 },
  { r: 128, g: 128, b: 128 },
  { r: 0, g: 0, b: 0 },
];

function rgbToHex(c: RGBColor): string {
  return `#${[c.r, c.g, c.b].map((v) => v.toString(16).padStart(2, "0")).join("")}`;
}

function hexToRgb(hex: string): RGBColor | null {
  const m = hex.match(/^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i);
  if (!m) return null;
  return { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) };
}

function rgbToHsl(c: RGBColor): { h: number; s: number; l: number } {
  const r = c.r / 255, g = c.g / 255, b = c.b / 255;
  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  const l = (max + min) / 2;
  if (max === min) return { h: 0, s: 0, l };
  const d = max - min;
  const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
  let h = 0;
  if (max === r) h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
  else if (max === g) h = ((b - r) / d + 2) / 6;
  else h = ((r - g) / d + 4) / 6;
  return { h: Math.round(h * 360), s: Math.round(s * 100), l: Math.round(l * 100) };
}

function hslToRgb(h: number, s: number, l: number): RGBColor {
  const s1 = s / 100, l1 = l / 100;
  if (s1 === 0) { const v = Math.round(l1 * 255); return { r: v, g: v, b: v }; }
  const hue2rgb = (p: number, q: number, t: number) => {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1/6) return p + (q - p) * 6 * t;
    if (t < 1/2) return q;
    if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
    return p;
  };
  const q = l1 < 0.5 ? l1 * (1 + s1) : l1 + s1 - l1 * s1;
  const p = 2 * l1 - q;
  const h1 = h / 360;
  return {
    r: Math.round(hue2rgb(p, q, h1 + 1/3) * 255),
    g: Math.round(hue2rgb(p, q, h1) * 255),
    b: Math.round(hue2rgb(p, q, h1 - 1/3) * 255),
  };
}

interface ColorPickerProps {
  color: RGBColor;
  onChange: (color: RGBColor) => void;
  className?: string;
}

export function ColorPicker({ color, onChange, className }: ColorPickerProps) {
  const [hex, setHex] = useState(rgbToHex(color));
  const hsl = rgbToHsl(color);

  useEffect(() => {
    setHex(rgbToHex(color));
  }, [color]);

  const handleHexChange = useCallback((value: string) => {
    setHex(value);
    const parsed = hexToRgb(value);
    if (parsed) onChange(parsed);
  }, [onChange]);

  const satLightRef = useRef<HTMLDivElement>(null);
  const dragging = useRef(false);

  const handleSatLightMove = useCallback((e: React.MouseEvent | MouseEvent) => {
    if (!satLightRef.current) return;
    const rect = satLightRef.current.getBoundingClientRect();
    const s = Math.max(0, Math.min(100, ((e.clientX - rect.left) / rect.width) * 100));
    const l = Math.max(0, Math.min(100, 100 - ((e.clientY - rect.top) / rect.height) * 100));
    onChange(hslToRgb(hsl.h, Math.round(s), Math.round(l)));
  }, [hsl.h, onChange]);

  const handleSatLightDown = useCallback((e: React.MouseEvent) => {
    dragging.current = true;
    handleSatLightMove(e);
    const handleUp = () => { dragging.current = false; window.removeEventListener("mouseup", handleUp); window.removeEventListener("mousemove", handleMove); };
    const handleMove = (ev: MouseEvent) => { if (dragging.current) handleSatLightMove(ev); };
    window.addEventListener("mouseup", handleUp);
    window.addEventListener("mousemove", handleMove);
  }, [handleSatLightMove]);

  return (
    <Popover>
      <PopoverTrigger
        render={
          <Button variant="outline" className={cn("h-8 w-16 p-0 border", className)}>
            <div
              className="h-full w-full rounded-[calc(var(--radius)-4px)]"
              style={{ backgroundColor: rgbToHex(color) }}
            />
          </Button>
        }
      />
      <PopoverContent className="w-64" align="start">
        <div className="flex flex-col gap-3">
          <div
            ref={satLightRef}
            className="relative h-32 w-full cursor-crosshair rounded-md border"
            style={{
              background: `linear-gradient(to top, #000, transparent), linear-gradient(to right, #fff, hsl(${hsl.h}, 100%, 50%))`,
            }}
            onMouseDown={handleSatLightDown}
          >
            <div
              className="absolute size-3 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white shadow-sm"
              style={{
                left: `${hsl.s}%`,
                top: `${100 - hsl.l}%`,
                backgroundColor: rgbToHex(color),
              }}
            />
          </div>

          <input
            type="range"
            min={0}
            max={360}
            value={hsl.h}
            onChange={(e) => onChange(hslToRgb(Number(e.target.value), hsl.s, hsl.l))}
            className="h-3 w-full appearance-none rounded-full"
            style={{
              background: "linear-gradient(to right, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00)",
            }}
          />

          <div className="flex flex-wrap gap-1">
            {PRESET_COLORS.map((c, i) => (
              <button
                key={i}
                className={cn(
                  "size-6 rounded-md border transition-transform hover:scale-110",
                  rgbToHex(c) === rgbToHex(color) && "ring-2 ring-primary ring-offset-1",
                )}
                style={{ backgroundColor: rgbToHex(c) }}
                onClick={() => onChange(c)}
              />
            ))}
          </div>

          <div className="grid grid-cols-4 gap-2">
            <div className="col-span-2">
              <Label className="text-xs">Hex</Label>
              <Input
                value={hex}
                onChange={(e) => handleHexChange(e.target.value)}
                className="h-7 text-xs font-mono"
              />
            </div>
            <div>
              <Label className="text-xs">R</Label>
              <Input
                type="number"
                min={0}
                max={255}
                value={color.r}
                onChange={(e) => onChange({ ...color, r: Number(e.target.value) })}
                className="h-7 text-xs font-mono"
              />
            </div>
            <div>
              <Label className="text-xs">G</Label>
              <Input
                type="number"
                min={0}
                max={255}
                value={color.g}
                onChange={(e) => onChange({ ...color, g: Number(e.target.value) })}
                className="h-7 text-xs font-mono"
              />
            </div>
          </div>
        </div>
      </PopoverContent>
    </Popover>
  );
}
