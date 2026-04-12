import type { CSSProperties, MouseEvent, ReactNode } from "react";
import { isValidElement, memo, useId, useMemo } from "react";
import {
  Tooltip,
  TooltipTrigger,
  TooltipContent,
} from "@/components/ui/tooltip";
import type { KeyboardKey as KeyboardKeyModel } from "../types";
import { LEGEND_POSITION_CLASSES, cn, getKeyGeometry, getKeySvgGeometry, safeColor } from "../utils";

interface KeyboardKeyProps {
  keyData: KeyboardKeyModel;
  unit: number;
  gap: number;
  offsetX: number;
  offsetY: number;
  selected?: boolean;
  interactive?: boolean;
  showTooltip?: boolean;
  showLegendSlots?: boolean;
  onClick?: (key: KeyboardKeyModel) => void;
  renderLegend?: (args: {
    key: KeyboardKeyModel;
    label: string;
    index: number;
  }) => ReactNode;
  overrideColor?: string;
  primaryLegend?: ReactNode;
  legendSlots?: Array<ReactNode | undefined>;
  primaryLegendClassName?: string;
  primaryLegendColor?: string;
  primaryLegendFontSize?: number;
  theme?: string;
}

function isDefaultLegendColor(color: string | undefined): boolean {
  if (!color) return true;
  const c = color.trim().toLowerCase();
  return c === "" || c === "#111827" || c === "#000000" || c === "#000";
}

function getLegendStyle(
  key: KeyboardKeyModel,
  index: number,
  theme?: string,
  overrideColor?: string,
  overrideFontSize?: number,
): CSSProperties {
  const rawColor = overrideColor ?? key.textColor[index] ?? key.defaultLegendColor;
  const size = overrideFontSize ?? key.textSize[index] ?? key.defaultLegendSize;

  const style: CSSProperties = {
    fontSize: `${Math.max(10, size * 4)}px`,
  };

  if (theme === "modern" && isDefaultLegendColor(rawColor)) {
    return style;
  }

  style.color = safeColor(rawColor, key.defaultLegendColor);
  return style;
}

function isDefaultKeyColor(color: string): boolean {
  return color.trim().toLowerCase() === "#cccccc";
}

function getLegendText(value: ReactNode | undefined): string | undefined {
  if (typeof value === "string") return value;
  if (typeof value === "number") return String(value);

  if (isValidElement<{ [key: string]: unknown }>(value)) {
    const fromData = value.props["data-keycap-text"];
    if (typeof fromData === "string" && fromData.trim().length > 0) {
      return fromData;
    }

    const ariaLabel = value.props["aria-label"];
    if (typeof ariaLabel === "string" && ariaLabel.trim().length > 0) {
      return ariaLabel;
    }
  }

  return undefined;
}

function KeyboardKeyComponent({
  keyData,
  unit,
  gap,
  offsetX,
  offsetY,
  selected,
  interactive,
  showTooltip = true,
  showLegendSlots,
  onClick,
  renderLegend,
  overrideColor,
  primaryLegend,
  legendSlots,
  primaryLegendClassName,
  primaryLegendColor,
  primaryLegendFontSize,
  theme,
}: KeyboardKeyProps) {
  const reactId = useId().replace(/:/g, "");
  const geometry = getKeyGeometry(keyData);
  const shape = useMemo(() => getKeySvgGeometry(keyData, unit), [keyData, unit]);
  const bounds = geometry.bounds;
  const rotationOriginX = (keyData.rotationX - bounds.x) * unit;
  const rotationOriginY = (keyData.rotationY - bounds.y) * unit;
  const hasCustomColor = overrideColor != null || !isDefaultKeyColor(keyData.color);

  const wrapperStyle: CSSProperties = {
    left: (bounds.x - offsetX) * unit,
    top: (bounds.y - offsetY) * unit,
    width: shape.width,
    height: shape.height,
    transform: keyData.rotationAngle ? `rotate(${keyData.rotationAngle}deg)` : undefined,
    transformOrigin: `${rotationOriginX}px ${rotationOriginY}px`,
    zIndex: selected ? 3 : keyData.decal ? 0 : 1,
  };

  const appearanceStyle: CSSProperties = {
    ["--kle-key-opacity" as string]: keyData.ghost ? 0.5 : 1,
    ["--kle-key-saturation" as string]: keyData.decal ? 0.25 : 1,
    ["--kle-gap" as string]: `${gap}px`,
    ["--kle-top-inset" as string]: `${shape.topInset}px`,
    ["--kle-key-color" as string]: overrideColor ?? (hasCustomColor ? keyData.color : undefined),
  };

  const handleClick = (event: MouseEvent<HTMLButtonElement | HTMLDivElement>) => {
    if (!interactive || !onClick) return;
    event.preventDefault();
    onClick(keyData);
  };

  const commonKeyClasses = cn(
    "kle-key",
    selected && "kle-key--selected",
    keyData.ghost && "kle-key--ghost",
    keyData.decal && "kle-key--decal",
    keyData.stepped && "kle-key--stepped",
    keyData.nub && "kle-key--nub",
    interactive && "kle-key--interactive",
  );

  const slotCount = Math.max(
    keyData.labels.length,
    legendSlots?.length ?? 0,
    showLegendSlots ? 12 : 0,
  );

  const legends = Array.from({ length: slotCount }, (_, index) => {
    const label = keyData.labels[index] ?? "";
    const slotLegend = legendSlots?.[index];
    const hasSlotLegendOverride = slotLegend !== undefined;
    const hasPrimaryLegendOverride = index === 0 && primaryLegend !== undefined;

    if (!label && !hasPrimaryLegendOverride && !hasSlotLegendOverride) {
      return showLegendSlots ? (
        <span key={index} className={cn("kle-legend kle-legend--placeholder", LEGEND_POSITION_CLASSES[index])} />
      ) : null;
    }

    const legendContent = hasSlotLegendOverride
      ? slotLegend
      : hasPrimaryLegendOverride
        ? primaryLegend
        : renderLegend
          ? renderLegend({ key: keyData, label, index })
          : label;

    return (
      <span
        key={index}
        className={cn(
          "kle-legend",
          LEGEND_POSITION_CLASSES[index],
          (hasPrimaryLegendOverride || hasSlotLegendOverride) && primaryLegendClassName,
        )}
        style={getLegendStyle(
          keyData,
          index,
          theme,
          index === 0 ? primaryLegendColor : undefined,
          index === 0 ? primaryLegendFontSize : undefined,
        )}
      >
        {legendContent}
      </span>
    );
  });

  const effectiveLegendTexts = Array.from({ length: slotCount }, (_, index) => {
    const label = keyData.labels[index] ?? "";
    const slotLegend = legendSlots?.[index];
    if (slotLegend !== undefined) {
      const slotText = getLegendText(slotLegend);
      if (slotText && slotText.trim().length > 0) return slotText;
      return undefined;
    }
    if (index === 0 && primaryLegend !== undefined) {
      const primaryText = getLegendText(primaryLegend);
      if (primaryText && primaryText.trim().length > 0) return primaryText;
      return undefined;
    }
    return label;
  });

  const label = effectiveLegendTexts[4] || effectiveLegendTexts[0] || keyData.id;
  const tooltipText = effectiveLegendTexts
    .filter((value): value is string => Boolean(value && value.trim().length > 0))
    .map((l) => l.replace(/\n/g, " ").trim())
    .join(" / ");
  const clipId = `kle-top-clip-${reactId}`;
  const content = (
    <>
      <svg
        className="kle-key-svg"
        viewBox={`0 0 ${shape.width} ${shape.height}`}
        width={shape.width}
        height={shape.height}
        aria-hidden="true"
      >
        <defs>
          <clipPath id={clipId}>
            <path d={shape.innerPath} />
          </clipPath>
        </defs>

        {selected ? <path className="kle-key-ring" d={shape.outerPath} /> : null}

        <g className="kle-key-shape kle-key-shape--base">
          <path className="kle-key-shape-outer" d={shape.outerPath} />
          <path className="kle-key-stroke-outer" d={shape.outerPath} />
        </g>

        <g className="kle-key-shape kle-key-shape--top">
          <path className="kle-key-shape-inner" d={shape.innerPath} />
          <path className="kle-key-stroke-inner" d={shape.innerPath} />

          {keyData.stepped ? (
            <rect
              className="kle-key-stepped-indicator"
              x={shape.width * 0.18}
              y={shape.height * 0.77}
              width={shape.width * 0.64}
              height={Math.max(2, unit * 0.04)}
              rx={999}
              clipPath={`url(#${clipId})`}
            />
          ) : null}

          {keyData.nub ? (
            <rect
              className="kle-key-nub-indicator"
              x={shape.width * 0.41}
              y={shape.height * 0.8}
              width={shape.width * 0.18}
              height={Math.max(4, unit * 0.07)}
              rx={999}
              clipPath={`url(#${clipId})`}
            />
          ) : null}
        </g>
      </svg>

      <span className="kle-legends">{legends}</span>
    </>
  );

  const keyElement = interactive ? (
    <button
      data-kle-key-id={keyData.id}
      className={commonKeyClasses}
      style={appearanceStyle}
      onClick={handleClick}
      type="button"
      aria-label={label}
      aria-pressed={selected || undefined}
    >
      {content}
    </button>
  ) : (
    <div
      data-kle-key-id={keyData.id}
      className={commonKeyClasses}
      style={appearanceStyle}
      onClick={handleClick}
      role="img"
      aria-label={label}
    >
      {content}
    </div>
  );

  if (!showTooltip || !tooltipText) {
    return <div className="kle-key-wrapper" data-kle-key-id={keyData.id} style={wrapperStyle}>{keyElement}</div>;
  }

  return (
    <div className="kle-key-wrapper" data-kle-key-id={keyData.id} style={wrapperStyle}>
      <Tooltip>
        <TooltipTrigger render={keyElement} />
        <TooltipContent side="top" sideOffset={6}>
          {tooltipText}
        </TooltipContent>
      </Tooltip>
    </div>
  );
}

export const KeyboardKey = memo(KeyboardKeyComponent, (prev, next) => (
  prev.keyData === next.keyData &&
  prev.unit === next.unit &&
  prev.gap === next.gap &&
  prev.offsetX === next.offsetX &&
  prev.offsetY === next.offsetY &&
  prev.selected === next.selected &&
  prev.interactive === next.interactive &&
  prev.showTooltip === next.showTooltip &&
  prev.showLegendSlots === next.showLegendSlots &&
  prev.onClick === next.onClick &&
  prev.renderLegend === next.renderLegend &&
  prev.overrideColor === next.overrideColor &&
  prev.primaryLegend === next.primaryLegend &&
  prev.legendSlots === next.legendSlots &&
  prev.primaryLegendClassName === next.primaryLegendClassName &&
  prev.primaryLegendColor === next.primaryLegendColor &&
  prev.primaryLegendFontSize === next.primaryLegendFontSize &&
  prev.theme === next.theme
));

KeyboardKey.displayName = "KeyboardKey";
