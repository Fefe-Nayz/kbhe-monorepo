import { describe, expect, test } from "bun:test";

import {
  isAreaSelectionGesture,
  normalizeSelectionRect,
  selectIntersectingKeyIds,
} from "./base-keyboard-selection";

describe("BaseKeyboard area selection", () => {
  test("normalizes drags in every direction", () => {
    expect(normalizeSelectionRect({ x: 80, y: 60 }, { x: 20, y: 10 })).toEqual({
      left: 20,
      top: 10,
      right: 80,
      bottom: 60,
    });
  });

  test("distinguishes a click from an actual drag", () => {
    expect(isAreaSelectionGesture({ x: 10, y: 10 }, { x: 12, y: 12 })).toBeFalse();
    expect(isAreaSelectionGesture({ x: 10, y: 10 }, { x: 14, y: 10 })).toBeTrue();
  });

  test("selects intersecting keyboard keys once and ignores non-key targets", () => {
    const ids = selectIntersectingKeyIds(
      { left: 10, top: 10, right: 90, bottom: 60 },
      [
        { id: "key-0", rect: { left: 0, top: 0, right: 40, bottom: 40 } },
        { id: "key-0", rect: { left: 4, top: 4, right: 36, bottom: 36 } },
        { id: "key-1", rect: { left: 50, top: 20, right: 80, bottom: 50 } },
        { id: "key-2", rect: { left: 90, top: 10, right: 120, bottom: 40 } },
        { id: "rotary.press", rect: { left: 20, top: 20, right: 30, bottom: 30 } },
      ],
    );

    expect(ids).toEqual(["key-0", "key-1"]);
  });
});
