import { describe, expect, test } from "bun:test";

import {
  decodeProfileWalRecord,
  encodeProfileWalRecord,
} from "./profile-persistence";

describe("profile persistence recovery log", () => {
  test("round-trips both writes and durable deletion tombstones", () => {
    expect(decodeProfileWalRecord(encodeProfileWalRecord(
      "keyboard-profile:Gaming",
      "{\"schemaVersion\":2}",
    ))).toEqual({
      key: "keyboard-profile:Gaming",
      value: "{\"schemaVersion\":2}",
    });
    expect(decodeProfileWalRecord(encodeProfileWalRecord(
      "keyboard-device-profile:serial%3Aabc:2",
      null,
    ))).toEqual({
      key: "keyboard-device-profile:serial%3Aabc:2",
      value: null,
    });
  });

  test("rejects malformed and out-of-scope recovery records", () => {
    expect(decodeProfileWalRecord("not-json")).toBeNull();
    expect(decodeProfileWalRecord(JSON.stringify({
      key: "unrelated-setting",
      value: "x",
    }))).toBeNull();
    expect(decodeProfileWalRecord(JSON.stringify({
      key: "keyboard-profile:Gaming",
      value: 42,
    }))).toBeNull();
  });
});
