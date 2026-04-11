import type { CSSProperties, MouseEvent, ReactNode } from "react";
import { useId, useMemo } from "react";
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
  showLegendSlots?: boolean;
  onClick?: (key: KeyboardKeyModel) => void;
  renderLegend?: (args: {
    key: KeyboardKeyModel;
    label: string;
    index: number;
  }) => ReactNode;
}

function getLegendStyle(key: KeyboardKeyModel, index: number): CSSProperties {
  const color = safeColor(key.textColor[index], key.defaultLegendColor);
  const size = key.textSize[index] ?? key.defaultLegendSize;

  return {
    color,
    fontSize: `${Math.max(10, size * 4)}px`,
  };
}

function isDefaultKeyColor(color: string): boolean {
  return color.trim().toLowerCase() === "#cccccc";
}

export function KeyboardKey({
  keyData,
  unit,
  gap,
  offsetX,
  offsetY,
  selected,
  interactive,
  showLegendSlots,
  onClick,
  renderLegend,
}: KeyboardKeyProps) {
  const reactId = useId().replace(/:/g, "");
  const geometry = getKeyGeometry(keyData);
  const shape = useMemo(() => getKeySvgGeometry(keyData, unit), [keyData, unit]);
  const bounds = geometry.bounds;
  const rotationOriginX = (keyData.rotationX - bounds.x) * unit;
  const rotationOriginY = (keyData.rotationY - bounds.y) * unit;
  const hasCustomColor = !isDefaultKeyColor(keyData.color);

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
    ["--kle-key-color" as string]: hasCustomColor ? keyData.color : undefined,
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

  const legends = keyData.labels.map((label, index) => {
    if (!label) {
      return showLegendSlots ? (
        <span key={index} className={cn("kle-legend kle-legend--placeholder", LEGEND_POSITION_CLASSES[index])} />
      ) : null;
    }

    return (
      <span
        key={index}
        className={cn("kle-legend", LEGEND_POSITION_CLASSES[index])}
        style={getLegendStyle(keyData, index)}
      >
        {renderLegend ? renderLegend({ key: keyData, label, index }) : label}
      </span>
    );
  });

  const label = keyData.labels[4] || keyData.labels[0] || keyData.id;
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

  return (
    <div className="kle-key-wrapper" style={wrapperStyle}>
      {interactive ? (
        <button
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
        <div className={commonKeyClasses} style={appearanceStyle} onClick={handleClick} role="img" aria-label={label}>
          {content}
        </div>
      )}
    </div>
  );
}
