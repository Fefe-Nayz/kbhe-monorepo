import { describe, expect, test } from "bun:test";

import {
  selectKbheSessionDevice,
  selectKbheRecoveryTarget,
  type KbheDeviceKind,
  type KbheTransportDeviceInfo,
} from "./transport";

function keyboard(
  path: string,
  serialNumber: string | null,
  kind: KbheDeviceKind = "runtime",
): KbheTransportDeviceInfo {
  return {
    path,
    vid: 0x9172,
    pid: kind === "runtime" ? 0x0002 : 0x0003,
    kind,
    interfaceNumber: 1,
    usagePage: 0xff00,
    usage: 1,
    manufacturer: "KBHE",
    product: "KBHE 75HE",
    serialNumber,
  };
}

describe("KBHE session device selection", () => {
  test("selects one serialised keyboard and preserves an explicit identity", () => {
    const target = keyboard("target-path", "TARGET");
    const other = keyboard("other-path", "OTHER");

    expect(selectKbheSessionDevice([target])).toBe(target);
    expect(selectKbheSessionDevice([other, target], " TARGET ")).toBe(target);
    expect(selectKbheSessionDevice([], "TARGET")).toBeNull();
  });

  test("refuses enumeration-order selection across physical keyboards", () => {
    const first = keyboard("first", "FIRST");
    const second = keyboard("second", "SECOND");

    expect(() => selectKbheSessionDevice([first, second])).toThrow("multiple KBHE keyboards");
  });

  test("refuses missing and duplicate serial identities", () => {
    expect(() => selectKbheSessionDevice([keyboard("legacy", null)])).toThrow(
      "no stable USB serial number",
    );

    const runtime = keyboard("runtime", "DUP");
    const updater = keyboard("updater", "DUP", "updater");
    expect(() => selectKbheSessionDevice([runtime, updater], "DUP")).toThrow(
      "ambiguous KBHE target",
    );
  });
});

describe("KBHE recovery target selection", () => {
  test("accepts an updater-only keyboard without a configuration session", () => {
    const updater = keyboard("updater", "RECOVERY", "updater");
    expect(selectKbheRecoveryTarget([updater])).toEqual({ state: "ready", device: updater });
  });

  test("reports an updater with no serial as unsafe instead of guessing its path", () => {
    const target = selectKbheRecoveryTarget([keyboard("updater", null, "updater")]);
    expect(target.state).toBe("unsafe");
    if (target.state === "unsafe") {
      expect(target.reason).toContain("no stable USB serial number");
    }
  });

  test("refuses transient runtime/updater duplicates for the same serial", () => {
    const target = selectKbheRecoveryTarget([
      keyboard("runtime", "DUP"),
      keyboard("updater", "DUP", "updater"),
    ], "DUP");
    expect(target.state).toBe("unsafe");
  });
});
