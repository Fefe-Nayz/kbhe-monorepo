import type {
  KeyboardBounds,
  KeyboardKey,
  KeyRect,
  KeySvgGeometry,
  KeyVisualGeometry,
} from "./types";

const TWO_PIECE_EPSILON = 0.0001;

interface Point {
  x: number;
  y: number;
}

export function cn(...parts: Array<string | false | null | undefined>): string {
  return parts.filter(Boolean).join(" ");
}

export function safeColor(input: string | undefined, fallback: string): string {
  if (!input) return fallback;
  return input;
}

function rectUnion(a: KeyRect, b: KeyRect): KeyRect {
  const minX = Math.min(a.x, b.x);
  const minY = Math.min(a.y, b.y);
  const maxX = Math.max(a.x + a.width, b.x + b.width);
  const maxY = Math.max(a.y + a.height, b.y + b.height);

  return {
    x: minX,
    y: minY,
    width: maxX - minX,
    height: maxY - minY,
  };
}

function pointKey(point: Point): string {
  return `${point.x},${point.y}`;
}

function parsePointKey(key: string): Point {
  const [x, y] = key.split(",").map(Number);
  return { x, y };
}

function addBoundaryEdge(edges: Map<string, [Point, Point]>, a: Point, b: Point) {
  const forward = `${pointKey(a)}|${pointKey(b)}`;
  const reverse = `${pointKey(b)}|${pointKey(a)}`;

  if (edges.has(reverse)) {
    edges.delete(reverse);
    return;
  }

  edges.set(forward, [a, b]);
}

function isPointInsideRect(point: Point, rect: KeyRect): boolean {
  return (
    point.x > rect.x + TWO_PIECE_EPSILON &&
    point.x < rect.x + rect.width - TWO_PIECE_EPSILON &&
    point.y > rect.y + TWO_PIECE_EPSILON &&
    point.y < rect.y + rect.height - TWO_PIECE_EPSILON
  );
}

function pruneCollinearPoints(points: Point[]): Point[] {
  if (points.length <= 3) return points;

  const next: Point[] = [];

  for (let i = 0; i < points.length; i += 1) {
    const previous = points[(i - 1 + points.length) % points.length];
    const current = points[i];
    const after = points[(i + 1) % points.length];

    const sameX =
      Math.abs(previous.x - current.x) < TWO_PIECE_EPSILON &&
      Math.abs(current.x - after.x) < TWO_PIECE_EPSILON;
    const sameY =
      Math.abs(previous.y - current.y) < TWO_PIECE_EPSILON &&
      Math.abs(current.y - after.y) < TWO_PIECE_EPSILON;

    if (sameX || sameY) continue;
    next.push(current);
  }

  return next.length ? next : points;
}

function polygonArea(points: Point[]): number {
  let area = 0;

  for (let i = 0; i < points.length; i += 1) {
    const current = points[i];
    const next = points[(i + 1) % points.length];
    area += current.x * next.y - next.x * current.y;
  }

  return area / 2;
}

function traceUnionPolygon(rects: KeyRect[]): Point[] {
  if (!rects.length) return [];

  if (rects.length === 1) {
    const rect = rects[0];
    return [
      { x: rect.x, y: rect.y },
      { x: rect.x + rect.width, y: rect.y },
      { x: rect.x + rect.width, y: rect.y + rect.height },
      { x: rect.x, y: rect.y + rect.height },
    ];
  }

  const xs = [...new Set(rects.flatMap((rect) => [rect.x, rect.x + rect.width]))].sort((a, b) => a - b);
  const ys = [...new Set(rects.flatMap((rect) => [rect.y, rect.y + rect.height]))].sort((a, b) => a - b);
  const edges = new Map<string, [Point, Point]>();

  for (let yi = 0; yi < ys.length - 1; yi += 1) {
    for (let xi = 0; xi < xs.length - 1; xi += 1) {
      const x0 = xs[xi];
      const x1 = xs[xi + 1];
      const y0 = ys[yi];
      const y1 = ys[yi + 1];
      const center = { x: (x0 + x1) / 2, y: (y0 + y1) / 2 };

      if (!rects.some((rect) => isPointInsideRect(center, rect))) continue;

      addBoundaryEdge(edges, { x: x0, y: y0 }, { x: x1, y: y0 });
      addBoundaryEdge(edges, { x: x1, y: y0 }, { x: x1, y: y1 });
      addBoundaryEdge(edges, { x: x1, y: y1 }, { x: x0, y: y1 });
      addBoundaryEdge(edges, { x: x0, y: y1 }, { x: x0, y: y0 });
    }
  }

  const adjacency = new Map<string, Point[]>();

  for (const [a, b] of edges.values()) {
    const keyA = pointKey(a);
    const keyB = pointKey(b);

    adjacency.set(keyA, [...(adjacency.get(keyA) ?? []), b]);
    adjacency.set(keyB, [...(adjacency.get(keyB) ?? []), a]);
  }

  const startKey = [...adjacency.keys()].sort((left, right) => {
    const a = parsePointKey(left);
    const b = parsePointKey(right);
    return a.y - b.y || a.x - b.x;
  })[0];

  if (!startKey) return [];

  const points: Point[] = [parsePointKey(startKey)];
  let currentKey = startKey;
  let previousKey: string | null = null;

  while (true) {
    const neighbors = adjacency.get(currentKey) ?? [];
    const nextPoint = neighbors.find((candidate) => pointKey(candidate) !== previousKey);

    if (!nextPoint) break;

    const nextKey = pointKey(nextPoint);
    if (nextKey === startKey) break;

    points.push(nextPoint);
    previousKey = currentKey;
    currentKey = nextKey;
  }

  return pruneCollinearPoints(points);
}

function formatNumber(value: number): string {
  return Number(value.toFixed(3)).toString();
}

function buildRoundedPath(points: Point[], radius: number): string {
  if (!points.length) return "";
  if (points.length === 1) return `M ${formatNumber(points[0].x)} ${formatNumber(points[0].y)} Z`;

  const orientation = polygonArea(points) >= 0 ? 1 : -1;

  const corners = points.map((current, index) => {
    const previous = points[(index - 1 + points.length) % points.length];
    const next = points[(index + 1) % points.length];

    const inVector = { x: current.x - previous.x, y: current.y - previous.y };
    const outVector = { x: next.x - current.x, y: next.y - current.y };
    const inLength = Math.abs(inVector.x) + Math.abs(inVector.y);
    const outLength = Math.abs(outVector.x) + Math.abs(outVector.y);
    const inDirection = { x: inVector.x / inLength, y: inVector.y / inLength };
    const outDirection = { x: outVector.x / outLength, y: outVector.y / outLength };
    const cross = inVector.x * outVector.y - inVector.y * outVector.x;
    const convex = orientation > 0 ? cross > 0 : cross < 0;
    const cut = convex ? Math.min(radius, inLength / 2, outLength / 2) : 0;

    return {
      point: current,
      convex,
      start: {
        x: current.x - inDirection.x * cut,
        y: current.y - inDirection.y * cut,
      },
      end: {
        x: current.x + outDirection.x * cut,
        y: current.y + outDirection.y * cut,
      },
    };
  });

  const first = corners[0];
  let path = `M ${formatNumber(first.start.x)} ${formatNumber(first.start.y)}`;

  corners.forEach((corner, index) => {
    if (index > 0) {
      path += ` L ${formatNumber(corner.start.x)} ${formatNumber(corner.start.y)}`;
    }

    if (corner.convex) {
      path += ` Q ${formatNumber(corner.point.x)} ${formatNumber(corner.point.y)} ${formatNumber(corner.end.x)} ${formatNumber(corner.end.y)}`;
    } else {
      path += ` L ${formatNumber(corner.point.x)} ${formatNumber(corner.point.y)}`;
    }
  });

  path += ` L ${formatNumber(first.start.x)} ${formatNumber(first.start.y)} Z`;
  return path;
}

function shrinkRect(rect: KeyRect, inset: number): KeyRect | null {
  const width = rect.width - inset * 2;
  const height = rect.height - inset * 2;

  if (width <= TWO_PIECE_EPSILON || height <= TWO_PIECE_EPSILON) return null;

  return {
    x: rect.x + inset,
    y: rect.y + inset,
    width,
    height,
  };
}

export function getKeyGeometry(key: KeyboardKey): KeyVisualGeometry {
  const primary: KeyRect = {
    x: key.x,
    y: key.y,
    width: key.width,
    height: key.height,
  };

  const hasSecondary =
    Math.abs(key.x2) > TWO_PIECE_EPSILON ||
    Math.abs(key.y2) > TWO_PIECE_EPSILON ||
    Math.abs(key.width2 - key.width) > TWO_PIECE_EPSILON ||
    Math.abs(key.height2 - key.height) > TWO_PIECE_EPSILON;

  const secondary: KeyRect | null = hasSecondary
    ? {
        x: key.x + key.x2,
        y: key.y + key.y2,
        width: key.width2,
        height: key.height2,
      }
    : null;

  const bounds = secondary ? rectUnion(primary, secondary) : primary;

  return { bounds, primary, secondary };
}

export function getKeySvgGeometry(key: KeyboardKey, unit: number): KeySvgGeometry {
  const geometry = getKeyGeometry(key);
  const localRects = [geometry.primary, geometry.secondary]
    .filter((rect): rect is KeyRect => Boolean(rect))
    .map((rect) => ({
      x: (rect.x - geometry.bounds.x) * unit,
      y: (rect.y - geometry.bounds.y) * unit,
      width: rect.width * unit,
      height: rect.height * unit,
    }));

  const outerPolygon = traceUnionPolygon(localRects);
  const topInset = Math.max(4, unit * 0.095);
  const innerRects = localRects
    .map((rect) => shrinkRect(rect, topInset))
    .filter((rect): rect is KeyRect => Boolean(rect));
  const innerPolygon = traceUnionPolygon(innerRects.length ? innerRects : localRects);

  return {
    width: geometry.bounds.width * unit,
    height: geometry.bounds.height * unit,
    outerPath: buildRoundedPath(outerPolygon, Math.max(3, unit * 0.08)),
    innerPath: buildRoundedPath(innerPolygon, Math.max(2, unit * 0.06)),
    bounds: geometry.bounds,
    topInset,
  };
}

function rotatePoint(x: number, y: number, cx: number, cy: number, angleDeg: number) {
  const angle = (angleDeg * Math.PI) / 180;
  const cos = Math.cos(angle);
  const sin = Math.sin(angle);

  const dx = x - cx;
  const dy = y - cy;

  return {
    x: cx + dx * cos - dy * sin,
    y: cy + dx * sin + dy * cos,
  };
}

function getRotatedRectBounds(rect: KeyRect, pivotX: number, pivotY: number, angleDeg: number): KeyRect {
  if (!angleDeg) return rect;

  const points = [
    rotatePoint(rect.x, rect.y, pivotX, pivotY, angleDeg),
    rotatePoint(rect.x + rect.width, rect.y, pivotX, pivotY, angleDeg),
    rotatePoint(rect.x, rect.y + rect.height, pivotX, pivotY, angleDeg),
    rotatePoint(rect.x + rect.width, rect.y + rect.height, pivotX, pivotY, angleDeg),
  ];

  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);

  const minX = Math.min(...xs);
  const minY = Math.min(...ys);
  const maxX = Math.max(...xs);
  const maxY = Math.max(...ys);

  return {
    x: minX,
    y: minY,
    width: maxX - minX,
    height: maxY - minY,
  };
}

export function getKeyboardBounds(keys: KeyboardKey[]): KeyboardBounds {
  if (!keys.length) {
    return {
      minX: 0,
      minY: 0,
      maxX: 0,
      maxY: 0,
      width: 0,
      height: 0,
    };
  }

  const rects = keys.map((key) => {
    const geometry = getKeyGeometry(key);
    return getRotatedRectBounds(
      geometry.bounds,
      key.rotationX,
      key.rotationY,
      key.rotationAngle,
    );
  });

  const minX = Math.min(...rects.map((rect) => rect.x));
  const minY = Math.min(...rects.map((rect) => rect.y));
  const maxX = Math.max(...rects.map((rect) => rect.x + rect.width));
  const maxY = Math.max(...rects.map((rect) => rect.y + rect.height));

  return {
    minX,
    minY,
    maxX,
    maxY,
    width: maxX - minX,
    height: maxY - minY,
  };
}

export const LEGEND_POSITION_CLASSES = [
  "kle-legend--top-left",
  "kle-legend--top-center",
  "kle-legend--top-right",
  "kle-legend--middle-left",
  "kle-legend--center",
  "kle-legend--middle-right",
  "kle-legend--bottom-left",
  "kle-legend--bottom-center",
  "kle-legend--bottom-right",
  "kle-legend--front-left",
  "kle-legend--front-center",
  "kle-legend--front-right",
] as const;
