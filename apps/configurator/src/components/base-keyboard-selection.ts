export interface SelectionPoint {
  x: number;
  y: number;
}

export interface SelectionRect {
  left: number;
  top: number;
  right: number;
  bottom: number;
}

export interface SelectableKeyRect {
  id: string;
  rect: SelectionRect;
}

const KEY_ID_PATTERN = /^key-\d+$/;

export function normalizeSelectionRect(
  start: SelectionPoint,
  end: SelectionPoint,
): SelectionRect {
  return {
    left: Math.min(start.x, end.x),
    top: Math.min(start.y, end.y),
    right: Math.max(start.x, end.x),
    bottom: Math.max(start.y, end.y),
  };
}

export function isAreaSelectionGesture(
  start: SelectionPoint,
  end: SelectionPoint,
  minimumDistance = 4,
): boolean {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  return dx * dx + dy * dy >= minimumDistance * minimumDistance;
}

export function selectIntersectingKeyIds(
  selection: SelectionRect,
  keys: Iterable<SelectableKeyRect>,
): string[] {
  const selected: string[] = [];
  const seen = new Set<string>();

  for (const key of keys) {
    if (!KEY_ID_PATTERN.test(key.id) || seen.has(key.id)) continue;

    const intersects =
      key.rect.left < selection.right
      && key.rect.right > selection.left
      && key.rect.top < selection.bottom
      && key.rect.bottom > selection.top;

    if (intersects) {
      seen.add(key.id);
      selected.push(key.id);
    }
  }

  return selected;
}
