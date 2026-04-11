import { useMemo } from "react";
import { ensureParsedKeyboardLayout } from "../parser";
import type { KeyboardLayoutProps } from "../types";
import { cn, getKeyboardBounds } from "../utils";
import { KeyboardKey } from "./KeyboardKey";

export function KeyboardLayout({
  layout,
  className,
  unit = 56,
  gap = 6,
  showLegendSlots = false,
  interactive = false,
  selectedKeyId,
  selectedKeyIds,
  theme = "kle",
  onKeyClick,
  renderLegend,
  keyColorMap,
}: KeyboardLayoutProps) {
  const parsed = useMemo(() => ensureParsedKeyboardLayout(layout), [layout]);
  const bounds = useMemo(() => getKeyboardBounds(parsed.keys), [parsed.keys]);

  const framePadding = 18;
  const frameBorder = 1;
  const width = Math.max(0, bounds.width * unit);
  const height = Math.max(0, bounds.height * unit);
  const framedWidth = width + framePadding * 2 + frameBorder * 2;
  const framedHeight = height + framePadding * 2 + frameBorder * 2;

  return (
    <div
      className={cn("kle-root", className)}
      data-kle-theme={theme}
      style={{
        width: framedWidth,
        minHeight: framedHeight,
        backgroundColor: theme === "kle" ? parsed.meta.backcolor : undefined,
      }}
    >
      <div
        className="kle-stage"
        style={{
          width,
          height,
        }}
      >
        {parsed.keys.map((key) => (
          <KeyboardKey
            key={key.id}
            keyData={key}
            unit={unit}
            gap={gap}
            offsetX={bounds.minX}
            offsetY={bounds.minY}
            selected={
              selectedKeyIds?.includes(key.id) ?? selectedKeyId === key.id
            }
            interactive={interactive}
            onClick={onKeyClick}
            showLegendSlots={showLegendSlots}
            renderLegend={renderLegend}
            overrideColor={keyColorMap?.[key.id]}
            theme={theme}
          />
        ))}
      </div>
    </div>
  );
}
