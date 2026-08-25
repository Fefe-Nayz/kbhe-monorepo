import { describe, expect, test } from "bun:test";

import {
  compatibilityPresentation,
  evaluateDeviceCompatibility,
  runtimeSessionStatus,
} from "./compatibility";

describe("release compatibility matrix", () => {
  test.each([
    ["0.1.17", "2.0.8", 0x0003, "compatible"],
    ["0.1.99", "2.0.9", null, "compatible"],
    ["0.1.17", "2.0.10", 0x0003, "app-too-old"],
    ["0.1.18", "2.0.10", 0x0003, "compatible"],
    ["0.1.18", null, 0x0002, "compatible"],
    ["0.1.18", null, 0x0003, "compatible"],
    ["0.1.17", null, 0x0002, "compatible"],
    ["0.1.17", "2.0.0", null, "firmware-too-old"],
    ["0.1.17", "2.1.0", null, "app-too-old"],
    ["0.1.16", "2.0.8", null, "app-too-old"],
    ["0.1.17", null, 0x0004, "app-too-old"],
    ["0.1.17", null, 0x0001, "firmware-too-old"],
    ["0.1.17", null, null, "unknown"],
    ["not-semver", "2.0.8", null, "unknown"],
  ] as const)(
    "classifies app=%s firmware=%s updater=%s as %s",
    (appVersion, firmwareVersion, updaterProtocol, expected) => {
      const compatibility = evaluateDeviceCompatibility({
        appVersion,
        firmwareVersion,
        updaterProtocol,
      });
      expect(compatibility.status).toBe(expected);
    },
  );

  test("maps every mismatch to recovery-only runtime and visible recovery actions", () => {
    for (const firmwareVersion of ["2.0.0", "2.1.0", "invalid"]) {
      const compatibility = evaluateDeviceCompatibility({
        appVersion: "0.1.17",
        firmwareVersion,
      });
      expect(runtimeSessionStatus(compatibility)).toBe("recovery-only");
      expect(compatibilityPresentation(compatibility).showFirmwareAction).toBeTrue();
    }
  });

  test("documents the updater v2 migration without weakening the matrix", () => {
    const compatibility = evaluateDeviceCompatibility({
      appVersion: "0.1.17",
      updaterProtocol: 0x0002,
    });
    expect(compatibility.status).toBe("compatible");
    expect(compatibility.reason).toContain("signed v2-to-v3 migration");
  });
});
