import { useMemo, type CSSProperties, type MouseEventHandler, type ReactNode } from "react";
import { KeyboardKey as KeyboardKeyComponent } from "@/lib/vendor/react-kle-modern/components/KeyboardKey";
import type { KeyboardKey as KeyboardKeyModel } from "@/lib/vendor/react-kle-modern/types";
import { cn } from "@/lib/utils";

const EMPTY_LABELS = Array.from({ length: 12 }, () => "");

function buildFallbackLabels(labelText: string): string[] {
    const lines = labelText
        .split("\n")
        .map((line) => line.trim())
        .filter(Boolean);

    const labels = [...EMPTY_LABELS];
    if (lines[0]) labels[0] = lines[0];
    if (lines[1]) labels[6] = lines[1];
    if (lines[2]) labels[2] = lines[2];
    if (lines[3]) labels[8] = lines[3];

    return labels;
}

interface KeycapButtonProps {
    keyId: string;
    legendSlots: Array<ReactNode | undefined>;
    labelText: string;
    selected?: boolean;
    className?: string;
    unit?: number;
    legendClassName?: string;
    onClick?: () => void;
    onContextMenu?: MouseEventHandler<HTMLDivElement>;
}

export function KeycapButton({
    keyId,
    legendSlots,
    labelText,
    selected = false,
    className,
    unit = 42,
    legendClassName = "text-[9px] leading-[1.05]",
    onClick,
    onContextMenu,
}: KeycapButtonProps) {
    const keyData = useMemo<KeyboardKeyModel>(() => ({
        id: keyId,
        x: 0,
        y: 0,
        width: 1,
        height: 1,
        x2: 0,
        y2: 0,
        width2: 1,
        height2: 1,
        rotationX: 0,
        rotationY: 0,
        rotationAngle: 0,
        color: "#cccccc",
        labels: buildFallbackLabels(labelText),
        textColor: [],
        textSize: [],
        defaultLegendColor: "#111827",
        defaultLegendSize: 3,
        profile: "",
        nub: false,
        stepped: false,
        decal: false,
        ghost: false,
        sm: "",
        sb: "",
        st: "",
    }), [keyId, labelText]);

    const rootStyle = useMemo<CSSProperties>(() => ({
        width: unit + 2,
        minHeight: unit + 2,
        ["--kle-frame-padding" as string]: "0px",
        border: "none",
    }), [unit]);

    return (
        <div
            className={cn("kle-root shrink-0 rounded-3xl p-0", className)}
            data-kle-theme="modern"
            style={rootStyle}
            onContextMenu={onContextMenu}
        >
            <div className="kle-stage" style={{ width: unit, height: unit }}>
                <KeyboardKeyComponent
                    keyData={keyData}
                    unit={unit}
                    gap={4}
                    offsetX={0}
                    offsetY={0}
                    selected={selected}
                    interactive={Boolean(onClick)}
                    onClick={() => onClick?.()}
                    legendSlots={legendSlots}
                    primaryLegendClassName={legendClassName}
                    theme="modern"
                />
            </div>
        </div>
    );
}
