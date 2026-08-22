import { describe, expect, test } from "bun:test";

import { buildFirmwareSignatureManifest } from "./firmware";

interface SigningVectors {
  publicKeyHex: string;
  firmware: {
    dataHex: string;
    version: string;
    manifestHex: string;
    signatureHex: string;
  };
}

function decodeHex(value: string): Uint8Array {
  if (value.length % 2 !== 0 || !/^[0-9a-f]*$/i.test(value)) {
    throw new Error("invalid test-vector hex");
  }
  return Uint8Array.from(
    Array.from({ length: value.length / 2 }, (_, index) =>
      Number.parseInt(value.slice(index * 2, index * 2 + 2), 16)),
  );
}

function encodeHex(value: Uint8Array): string {
  return Array.from(value, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

describe("firmware release signing contract", () => {
  test("matches the shared Python/C/Rust golden manifest", async () => {
    const vectors = await Bun.file(
      "../../firmware/tests/release_signing_vectors.json",
    ).json() as SigningVectors;
    const [major, minor, patch] = vectors.firmware.version
      .split(".")
      .map((part) => Number.parseInt(part, 10));
    const data = decodeHex(vectors.firmware.dataHex);
    const expected = decodeHex(vectors.firmware.manifestHex);
    const crc = new DataView(expected.buffer, expected.byteOffset, expected.byteLength)
      .getUint32(12, true);

    const manifest = await buildFirmwareSignatureManifest(
      data,
      { major, minor, patch },
      crc,
    );
    expect(encodeHex(manifest)).toBe(vectors.firmware.manifestHex);
    expect(vectors.publicKeyHex).toHaveLength(64);
    expect(vectors.firmware.signatureHex).toHaveLength(128);
  });
});
