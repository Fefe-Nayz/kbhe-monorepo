import { describe, expect, test } from "bun:test";

import {
  parseUpdaterResponse,
  resolveFirmwareTargetSnapshot,
  selectFirmwareTargetDevice,
} from "./firmware";
import type { KbheTransportDeviceInfo } from "./transport";

function device(
  kind: KbheTransportDeviceInfo["kind"],
  serialNumber: string | null,
  path: string,
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
    product: kind === "runtime" ? "KBHE" : "KBHE Updater",
    serialNumber,
  };
}

describe("updater response framing", () => {
  test("parses a strict 64-byte updater report", () => {
    const report = new Uint8Array(64);
    report.set([0x03, 0x44, 0x00, 0x02, 0x38, 0, 0, 0, 0xaa, 0xbb]);

    expect(parseUpdaterResponse(report, 0x03)).toMatchObject({
      command: 0x03,
      sequence: 0x44,
      status: 0,
      length: 2,
      offset: 0x38,
      payload: Uint8Array.from([0xaa, 0xbb]),
    });
  });

  test("accepts a 65-byte report only with report ID zero", () => {
    const report = new Uint8Array(65);
    report.set([0, 0x01, 0x02, 0, 0], 0);
    expect(parseUpdaterResponse(report).command).toBe(0x01);

    report[0] = 7;
    expect(() => parseUpdaterResponse(report)).toThrow("report ID");
  });

  test("rejects truncated and oversized reports", () => {
    expect(() => parseUpdaterResponse(new Uint8Array(8))).toThrow("report size");
    expect(() => parseUpdaterResponse(new Uint8Array(66))).toThrow("report size");
  });
});

describe("firmware target identity", () => {
  test("tracks the same serial across runtime/updater re-enumeration", () => {
    const other = device("runtime", "OTHER", "runtime-other");
    const runtime = device("runtime", "TARGET", "runtime-target");
    expect(
      resolveFirmwareTargetSnapshot([other, runtime], " TARGET ").runtime?.path,
    ).toBe("runtime-target");

    const updater = device("updater", "TARGET", "updater-target");
    expect(
      resolveFirmwareTargetSnapshot([other, updater], "TARGET").updater?.path,
    ).toBe("updater-target");
  });

  test("refuses missing, duplicated, or cross-mode identities", () => {
    expect(() => selectFirmwareTargetDevice([], "runtime", " ")).toThrow("serial number");
    expect(() =>
      selectFirmwareTargetDevice(
        [device("updater", "DUP", "a"), device("updater", "DUP", "b")],
        "updater",
        "DUP",
      ),
    ).toThrow("ambiguous");
    expect(() =>
      resolveFirmwareTargetSnapshot(
        [device("runtime", "BOTH", "r"), device("updater", "BOTH", "u")],
        "BOTH",
      ),
    ).toThrow("both runtime and updater");
  });
});
