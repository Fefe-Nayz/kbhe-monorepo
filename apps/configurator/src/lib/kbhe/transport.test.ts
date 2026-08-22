import { describe, expect, test } from "bun:test";

import {
  selectKbheSessionDevice,
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
