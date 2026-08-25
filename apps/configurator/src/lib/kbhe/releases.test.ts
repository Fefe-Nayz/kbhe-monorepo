import { describe, expect, test } from "bun:test";
import { canUpdaterIdentificationUnlockRelease } from "./releases";

function blockedRelease(migrationAvailable: boolean, bootloaderRefreshAvailable: boolean) {
  return {
    blockedReason: "release recovery roles are incomplete",
    migrationAvailable,
    bootloaderRefreshAvailable,
  };
}

describe("firmware release recovery actions", () => {
  test("does not offer updater identification when neither protocol is installable", () => {
    expect(canUpdaterIdentificationUnlockRelease(
      blockedRelease(false, false),
      null,
    )).toBeFalse();
  });

  test("offers updater identification when it can select one viable recovery path", () => {
    expect(canUpdaterIdentificationUnlockRelease(
      blockedRelease(true, false),
      null,
    )).toBeTrue();
    expect(canUpdaterIdentificationUnlockRelease(
      blockedRelease(false, true),
      null,
    )).toBeTrue();
  });

  test("does not identify an updater that is known or for a non-blocked release", () => {
    expect(canUpdaterIdentificationUnlockRelease(
      blockedRelease(true, false),
      0x0002,
    )).toBeFalse();
    expect(canUpdaterIdentificationUnlockRelease({
      ...blockedRelease(true, false),
      blockedReason: null,
    }, null)).toBeFalse();
  });
});
