import { describe, expect, test } from "bun:test";

interface FileScopeEntry {
  path?: string;
}

interface PermissionWithScope {
  identifier?: string;
  allow?: FileScopeEntry[];
}

interface TauriCapability {
  permissions?: Array<string | PermissionWithScope>;
}

describe("Tauri firmware file scope", () => {
  test("allows firmware images and their detached signatures", async () => {
    const capability = await Bun.file(
      "src-tauri/capabilities/default.json",
    ).json() as TauriCapability;
    const readFilePermission = capability.permissions?.find(
      (permission): permission is PermissionWithScope =>
        typeof permission !== "string"
        && permission.identifier === "fs:allow-read-file",
    );
    const paths = readFilePermission?.allow?.map(({ path }) => path);

    expect(paths).toContain("**/*.bin");
    expect(paths).toContain(
      "$TEMP/kbhe-configurator/firmware/*/kbhe-app.bin.sig",
    );
    expect(paths).toContain(
      "$TEMP/kbhe-configurator/firmware/*/kbhe-app-updater-v3.bin.sig",
    );
    expect(paths).not.toContain("**/*.bin.sig");

    const firmwarePage = await Bun.file("src/pages/Firmware.tsx").text();
    expect(firmwarePage).toContain('invoke<number[]>("kbhe_read_firmware_signature"');
    expect(firmwarePage).toContain("downloadFirmwareRelease(tag, 0x0003, true)");
    expect(firmwarePage).toContain('invoke("kbhe_boot_existing_application"');
    expect(firmwarePage).toContain("Recover runtime only");
    expect(firmwarePage).toContain("Continue updater refresh");
  });
});
