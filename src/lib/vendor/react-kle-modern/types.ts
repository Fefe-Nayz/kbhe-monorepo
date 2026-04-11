import type { ReactNode } from "react";

export type KLESerializedLayout = unknown[];
export type KeyboardRenderTheme = "kle" | "modern";

export interface KeyboardMeta {
  author: string;
  backcolor: string;
  background: { name: string; style: string } | null;
  name: string;
  notes: string;
  radii: string;
  switchBrand: string;
  switchMount: string;
  switchType: string;
}

export interface KeyboardKey {
  id: string;
  x: number;
  y: number;
  width: number;
  height: number;
  x2: number;
  y2: number;
  width2: number;
  height2: number;
  rotationX: number;
  rotationY: number;
  rotationAngle: number;
  color: string;
  labels: string[];
  textColor: Array<string | undefined>;
  textSize: Array<number | undefined>;
  defaultLegendColor: string;
  defaultLegendSize: number;
  profile: string;
  nub: boolean;
  stepped: boolean;
  decal: boolean;
  ghost: boolean;
  sm: string;
  sb: string;
  st: string;
}

export interface ParsedKeyboardLayout {
  meta: KeyboardMeta;
  keys: KeyboardKey[];
}

export interface KeyRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface KeyVisualGeometry {
  bounds: KeyRect;
  primary: KeyRect;
  secondary: KeyRect | null;
}

export interface KeySvgGeometry {
  width: number;
  height: number;
  outerPath: string;
  innerPath: string;
  bounds: KeyRect;
  topInset: number;
}

export interface KeyboardBounds {
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
  width: number;
  height: number;
}

export interface KeyboardLayoutProps {
  layout: string | KLESerializedLayout | ParsedKeyboardLayout;
  className?: string;
  unit?: number;
  gap?: number;
  showLegendSlots?: boolean;
  interactive?: boolean;
  selectedKeyId?: string;
  selectedKeyIds?: string[];
  theme?: KeyboardRenderTheme;
  onKeyClick?: (key: KeyboardKey) => void;
  renderLegend?: (args: {
    key: KeyboardKey;
    label: string;
    index: number;
  }) => ReactNode;
}
