export type CompatibilityStatus =
  | "compatible"
  | "firmware-too-old"
  | "app-too-old"
  | "unknown";

export interface DeviceCompatibility {
  status: CompatibilityStatus;
  reason: string;
  appVersion: string;
  firmwareVersion: string | null;
  updaterProtocol: number | null;
}

export interface CompatibilityInput {
  appVersion: string;
  firmwareVersion?: string | null;
  updaterProtocol?: number | null;
}

export interface CompatibilityPresentation {
  title: string;
  showFirmwareAction: boolean;
  showAppUpdateAction: boolean;
}

/**
 * First release governed by the explicit compatibility matrix. Older releases
 * are intentionally not reinterpreted retroactively.
 */
export const COMPATIBILITY_INTRODUCED_APP_VERSION = "0.1.17";
export const COMPATIBLE_FIRMWARE_MIN = "2.0.8";
export const COMPATIBLE_FIRMWARE_MAX_EXCLUSIVE = "2.1.0";
export const SUPPORTED_UPDATER_PROTOCOLS = [0x0002, 0x0003] as const;

interface Semver {
  major: number;
  minor: number;
  patch: number;
}

function parseSemver(value: string): Semver | null {
  const match = /^v?(\d+)\.(\d+)\.(\d+)$/.exec(value.trim());
  if (!match) return null;
  const parts = match.slice(1).map(Number);
  if (parts.some((part) => !Number.isSafeInteger(part))) return null;
  return { major: parts[0]!, minor: parts[1]!, patch: parts[2]! };
}

function compareSemver(left: Semver, right: Semver): number {
  return left.major - right.major
    || left.minor - right.minor
    || left.patch - right.patch;
}

function result(
  input: CompatibilityInput,
  status: CompatibilityStatus,
  reason: string,
): DeviceCompatibility {
  return {
    status,
    reason,
    appVersion: input.appVersion,
    firmwareVersion: input.firmwareVersion?.trim() || null,
    updaterProtocol: input.updaterProtocol ?? null,
  };
}

/**
 * Compatibility contract introduced with app 0.1.17 for firmware 2.0.x.
 *
 * Runtime mode is accepted only for the firmware schema this app was released
 * with. Updater protocols 2 and 3 are both intentional: v2 is handled only by
 * the signed v2-to-v3 migration path, while v3 is the normal signed updater.
 */
export function evaluateDeviceCompatibility(input: CompatibilityInput): DeviceCompatibility {
  const app = parseSemver(input.appVersion);
  const releaseApp = parseSemver(COMPATIBILITY_INTRODUCED_APP_VERSION)!;
  if (!app) {
    return result(input, "unknown", `App version “${input.appVersion}” is not a valid semantic version.`);
  }
  if (compareSemver(app, releaseApp) < 0) {
    return result(
      input,
      "app-too-old",
      `Configurator ${input.appVersion} predates the compatibility contract introduced in ${COMPATIBILITY_INTRODUCED_APP_VERSION}.`,
    );
  }

  const firmwareValue = input.firmwareVersion?.trim();
  if (firmwareValue) {
    const firmware = parseSemver(firmwareValue);
    const minimumFirmware = parseSemver(COMPATIBLE_FIRMWARE_MIN)!;
    const maximumFirmware = parseSemver(COMPATIBLE_FIRMWARE_MAX_EXCLUSIVE)!;
    if (!firmware) {
      return result(input, "unknown", `Firmware version “${firmwareValue}” could not be interpreted safely.`);
    }
    if (compareSemver(firmware, minimumFirmware) < 0) {
      return result(
        input,
        "firmware-too-old",
        `Firmware ${firmwareValue} is older than the supported ${COMPATIBLE_FIRMWARE_MIN} configuration schema.`,
      );
    }
    if (compareSemver(firmware, maximumFirmware) >= 0) {
      return result(
        input,
        "app-too-old",
        `Firmware ${firmwareValue} is outside this app's supported 2.0.x firmware line.`,
      );
    }
  }

  const updaterProtocol = input.updaterProtocol;
  if (updaterProtocol != null) {
    if (!Number.isInteger(updaterProtocol) || updaterProtocol < 0 || updaterProtocol > 0xffff) {
      return result(input, "unknown", "The updater returned an invalid protocol version.");
    }
    if (!SUPPORTED_UPDATER_PROTOCOLS.includes(updaterProtocol as 0x0002 | 0x0003)) {
      if (updaterProtocol > SUPPORTED_UPDATER_PROTOCOLS[SUPPORTED_UPDATER_PROTOCOLS.length - 1]!) {
        return result(
          input,
          "app-too-old",
          `Updater protocol 0x${updaterProtocol.toString(16).padStart(4, "0")} is newer than this app supports.`,
        );
      }
      return result(
        input,
        "firmware-too-old",
        `Updater protocol 0x${updaterProtocol.toString(16).padStart(4, "0")} predates the supported migration path.`,
      );
    }
  }

  if (!firmwareValue && updaterProtocol == null) {
    return result(input, "unknown", "Neither a runtime firmware version nor an updater protocol could be read.");
  }

  const updaterNote = updaterProtocol === 0x0002
    ? " Updater protocol 0x0002 will use the signed v2-to-v3 migration."
    : "";
  return result(input, "compatible", `This app and keyboard are compatible.${updaterNote}`);
}

export function compatibilityPresentation(
  compatibility: DeviceCompatibility,
): CompatibilityPresentation {
  switch (compatibility.status) {
    case "firmware-too-old":
      return {
        title: "Keyboard update required",
        showFirmwareAction: true,
        showAppUpdateAction: false,
      };
    case "app-too-old":
      return {
        title: "Configurator update required",
        showFirmwareAction: true,
        showAppUpdateAction: true,
      };
    case "unknown":
      return {
        title: "Compatibility could not be verified",
        showFirmwareAction: true,
        showAppUpdateAction: true,
      };
    case "compatible":
      return {
        title: "Compatible",
        showFirmwareAction: false,
        showAppUpdateAction: false,
      };
  }
}

export function runtimeSessionStatus(
  compatibility: DeviceCompatibility,
): "connected" | "recovery-only" {
  return compatibility.status === "compatible" ? "connected" : "recovery-only";
}
