import { describe, expect, test } from "bun:test";

import { actionProfileCacheScope } from "./action-runtime";

describe("action profile query scope", () => {
  test("isolates temporary app profiles that share one firmware slot", () => {
    expect(actionProfileCacheScope("app", 0, "Gaming"))
      .not.toBe(actionProfileCacheScope("app", 0, "Work"));
  });

  test("isolates durable device slots from temporary app runtimes", () => {
    expect(actionProfileCacheScope("device", 2, null)).toBe("device:2");
    expect(actionProfileCacheScope("app", 2, "Slot mirror")).toBe("app:Slot mirror");
  });
});
