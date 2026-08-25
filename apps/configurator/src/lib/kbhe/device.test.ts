import { describe, expect, test } from "bun:test";

import { KbheCommander } from "./commander";
import { KBHEDevice } from "./device";
import {
  ActionOpcode,
  actionProgramHash,
  defaultActionOverlayBinding,
  type ActionOverlayBinding,
} from "./action-program";
import {
  Command,
  DKS_DEFAULT_ACTION_BITMAP,
  KEY_COUNT,
  SettingsSaveState,
  Status,
  dksActionAtPhase,
  setDksActionAtPhase,
} from "./protocol";
import type { KbheTransport, KbheTransportDeviceInfo } from "./transport";

function actionOverlayGetResponse(
  profileIndex: number,
  overlayIndex: number,
  binding: ActionOverlayBinding,
): Uint8Array {
  const payload = [
    binding.enabled ? 1 : 0,
    binding.priority,
    binding.blendMode,
    binding.opacity,
    ...binding.color,
    binding.allKeys ? 1 : 0,
    binding.fadeInMs & 0xff,
    (binding.fadeInMs >>> 8) & 0xff,
    binding.fadeOutMs & 0xff,
    (binding.fadeOutMs >>> 8) & 0xff,
    ...binding.keyMask,
    binding.stateIndex,
    binding.activeValue,
    binding.followsState ? 1 : 0,
    0,
  ];
  return Uint8Array.from([
    Command.GET_ACTION_OVERLAY,
    Status.OK,
    profileIndex,
    overlayIndex,
    payload.length,
    ...payload,
  ]);
}

function runtimeDevice(path: string, serialNumber: string | null): KbheTransportDeviceInfo {
  return {
    path,
    vid: 0xcafe,
    pid: 0x4004,
    kind: "runtime",
    interfaceNumber: 1,
    usagePage: 0xff60,
    usage: 0x61,
    manufacturer: "KBHE",
    product: "KBHE Keyboard",
    serialNumber,
  };
}

describe("KBHEDevice runtime identity compatibility", () => {
  test("decodes the shipped 2.0.0 packed version and legacy identity offsets", async () => {
    const version = new Uint8Array(64);
    version.set([Command.GET_FIRMWARE_VERSION, Status.OK, 0x00, 0x02]);
    const identity = new Uint8Array(64);
    identity.set([Command.GET_DEVICE_INFO, Status.OK, 0x00, 0x02]);
    identity.set(new TextEncoder().encode("75HE-LEGACY\0"), 4);
    identity.set(new TextEncoder().encode("Legacy KBHE\0"), 4 + 26);
    const transport = {
      sendCommand: async (command: number) => (
        command === Command.GET_FIRMWARE_VERSION ? version : identity
      ),
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getFirmwareVersion()).toBe("2.0.0");
    expect(await device.getDeviceInfo()).toEqual({
      firmware_version: "2.0.0",
      serial_number: "75HE-LEGACY",
      keyboard_name: "Legacy KBHE",
    });
  });
});

describe("KBHEDevice bulk key parser", () => {
  test("decodes SOCD Neutral and disable-keyboard-on-gamepad flags", async () => {
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number, data: ArrayLike<number>) => {
        expect(command).toBe(Command.GET_ALL_KEY_SETTINGS);
        const start = Number(data[1] ?? 0);
        const count = Math.min(6, KEY_COUNT - start);
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = 0;
        response[2] = start;
        response[3] = count;
        for (let index = 0; index < count; index += 1) {
          const offset = 4 + index * 8;
          response[offset] = 0x04;
          response[offset + 2] = 10;
          response[offset + 3] = 10;
          response[offset + 4] = 5;
          response[offset + 5] = 5;
          response[offset + 6] = 0xff;
          response[offset + 7] = (4 << 2) | 0x02 | 0x01;
        }
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    const settings = await device.getAllKeySettings();
    expect(settings).toHaveLength(KEY_COUNT);
    expect(settings?.[0]?.socd_resolution).toBe(4);
    expect(settings?.[0]?.disable_kb_on_gamepad).toBeTrue();
    expect(settings?.[0]?.rapid_trigger_enabled).toBeTrue();
  });

  test("preserves the DKS action bitmap instead of treating it as travel", async () => {
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        expect(command).toBe(Command.GET_KEY_SETTINGS);
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = 0;
        response[2] = 7;
        response[5] = 0x04;
        response[7] = 20;
        response[8] = 18;
        response[20] = 0xe4;
        response[21] = 0x05;
        response[23] = 0x1b;
        response[24] = 0x06;
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    const settings = await device.getKeySettings(7);
    expect(settings?.dynamic_zones[0]).toEqual({ end_mm_tenths: 0xe4, hid_keycode: 0x05 });
    expect(settings?.dynamic_zones[1]).toEqual({ end_mm_tenths: 0x1b, hid_keycode: 0x06 });
  });

  test("packs each DKS phase into its own two-bit field", () => {
    expect([0, 1, 2, 3].map((phase) => dksActionAtPhase(DKS_DEFAULT_ACTION_BITMAP, phase)))
      .toEqual([1, 0, 0, 2]);
    const withBottomTap = setDksActionAtPhase(DKS_DEFAULT_ACTION_BITMAP, 1, 3);
    expect([0, 1, 2, 3].map((phase) => dksActionAtPhase(withBottomTap, phase)))
      .toEqual([1, 3, 0, 2]);
  });
});

describe("KBHEDevice gamepad parser", () => {
  test("requires and decodes the complete four-point curve", async () => {
    let complete = false;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        expect(command).toBe(Command.GET_GAMEPAD_SETTINGS);
        const response = new Uint8Array(complete ? 18 : 17);
        response[0] = command;
        response[1] = 0;
        response[2] = 2;
        response[3] = 1;
        response[4] = 1;
        response[5] = 1;
        if (complete) {
          const points = [[0, 0], [100, 80], [250, 180], [400, 255]] as const;
          points.forEach(([x, y], index) => {
            const offset = 6 + index * 3;
            response[offset] = x & 0xff;
            response[offset + 1] = (x >>> 8) & 0xff;
            response[offset + 2] = y;
          });
        }
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getGamepadSettings()).toBeNull();
    complete = true;
    const settings = await device.getGamepadSettings();
    expect(settings?.keyboard_routing).toBe(2);
    expect(settings?.api_mode).toBe(1);
    expect(settings?.curve_points.map((point) => point.x_01mm)).toEqual([0, 100, 250, 400]);
  });
});

describe("KBHEDevice rotary overlay settings", () => {
  test("decodes filled-only mode and defaults legacy responses to classic mode", async () => {
    let responseLength = 39;
    let filledOnly = 0;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        expect(command).toBe(Command.GET_ROTARY_ENCODER_SETTINGS);
        const response = new Uint8Array(responseLength);
        response[0] = command;
        response[1] = 0;
        response[4] = 1;
        response[5] = 1;
        response[38] = 2;
        if (responseLength >= 40) response[39] = filledOnly;
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect((await device.getRotaryEncoderSettings())?.progress_filled_only).toBeFalse();
    responseLength = 40;
    filledOnly = 1;
    expect((await device.getRotaryEncoderSettings())?.progress_filled_only).toBeTrue();
    filledOnly = 2;
    expect(await device.getRotaryEncoderSettings()).toBeNull();
  });

  test("encodes filled-only mode immediately after acceleration", async () => {
    let payload: number[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number, data: ArrayLike<number>) => {
        expect(command).toBe(Command.SET_ROTARY_ENCODER_SETTINGS);
        payload = Array.from(data);
        return Uint8Array.of(command, 0);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.setRotaryEncoderSettings({
      acceleration: 3,
      progress_filled_only: true,
    })).toBeTrue();
    expect(payload).toHaveLength(39);
    expect(payload.slice(-2)).toEqual([3, 1]);
  });
});

describe("KBHEDevice profile document CAS", () => {
  test("reads and commits the canonical generation with little-endian framing", async () => {
    const calls: Array<{ command: number; data: number[] }> = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number, data: ArrayLike<number>) => {
        calls.push({ command, data: Array.from(data) });
        const response = new Uint8Array(8);
        response[0] = command;
        response[1] = 0;
        response[2] = 2;
        response[3] = 3;
        response.set(command === Command.GET_PROFILE_DOCUMENT_META
          ? [0x78, 0x56, 0x34, 0x12]
          : [0x79, 0x56, 0x34, 0x12], 4);
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getProfileDocumentMeta(2)).toEqual({
      profileIndex: 2,
      schemaVersion: 3,
      generation: 0x12345678,
    });
    expect(await device.commitProfileDocument(2, 0x12345678)).toEqual({
      profileIndex: 2,
      schemaVersion: 3,
      generation: 0x12345679,
    });
    expect(calls).toEqual([
      { command: Command.GET_PROFILE_DOCUMENT_META, data: [1, 2] },
      { command: Command.COMMIT_PROFILE_DOCUMENT, data: [5, 2, 0x78, 0x56, 0x34, 0x12] },
    ]);
  });
});

describe("KBHEDevice real-time persistence telemetry", () => {
  test("decodes the extended 64-byte MCU metrics response", async () => {
    const response = new Uint8Array(64);
    const setU16 = (offset: number, value: number) => {
      response[offset] = value & 0xff;
      response[offset + 1] = (value >>> 8) & 0xff;
    };
    const setU32 = (offset: number, value: number) => {
      response[offset] = value & 0xff;
      response[offset + 1] = (value >>> 8) & 0xff;
      response[offset + 2] = (value >>> 16) & 0xff;
      response[offset + 3] = (value >>> 24) & 0xff;
    };
    response[0] = Command.GET_MCU_METRICS;
    response[1] = 0;
    setU16(19, 141);
    setU32(21, 7);
    setU32(25, 1234);
    setU32(29, 5678);
    setU32(33, 2);
    setU32(37, 1);
    setU32(41, 0);
    setU32(45, 3);
    setU16(49, 1);
    response[51] = 1;
    response[52] = 1;
    setU16(53, 100);
    response[55] = 0;
    setU16(56, 119);
    response[58] = 5;
    response[59] = 4;
    response[60] = 7;
    response[61] = 2;
    response[62] = 3;
    response[63] = 1;

    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async () => response,
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getMcuMetrics()).toMatchObject({
      realtime_persistence_metrics_available: true,
      max_scan_cycle_us: 141,
      p99_scan_cycle_us: 119,
      scan_deadline_miss_count: 7,
      flash_programmed_words: 1234,
      flash_async_steps: 5678,
      flash_gc_count: 2,
      flash_boot_erase_count: 1,
      flash_runtime_erase_count: 0,
      flash_deferred_no_space_count: 3,
      flash_max_words_per_step: 1,
      flash_async_busy: true,
      flash_spare_bank_ready: true,
      flash_word_program_datasheet_max_us: 100,
      flash_hard_8khz_guarantee: false,
      flash_last_status: 5,
      keyboard_queue_high_watermark: 4,
      nkro_queue_high_watermark: 7,
      keyboard_queue_overflow_count_sat: 2,
      nkro_queue_overflow_count_sat: 3,
      keyboard_transfer_failed_count_sat: 1,
    });
  });
});

describe("KBHEDevice settings durability", () => {
  test("keeps legacy SAVE_SETTINGS accepted semantics when no extension is present", async () => {
    const calls: number[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = 0;
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.saveSettings()).toBeTrue();
    expect(calls).toEqual([Command.SAVE_SETTINGS]);
  });

  test("waits until the firmware reports that settings are durable", async () => {
    const calls: number[] = [];
    let statusReads = 0;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = 0;
        response[3] = 1;
        if (command === Command.SAVE_SETTINGS) {
          response[2] = SettingsSaveState.Queued;
        } else {
          response[2] = statusReads++ === 0
            ? SettingsSaveState.Writing
            : SettingsSaveState.Durable;
          response[4] = response[2] === SettingsSaveState.Durable ? 0 : 1;
          response[6] = response[2] === SettingsSaveState.Writing ? 1 : 0;
        }
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.saveSettings()).toBeTrue();
    expect(calls).toEqual([
      Command.SAVE_SETTINGS,
      Command.GET_SETTINGS_SAVE_STATUS,
      Command.GET_SETTINGS_SAVE_STATUS,
    ]);
  });

  test("reports a durable-write failure instead of claiming success", async () => {
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = 0;
        response[2] = command === Command.SAVE_SETTINGS
          ? SettingsSaveState.Queued
          : SettingsSaveState.Failed;
        response[3] = 1;
        if (command === Command.GET_SETTINGS_SAVE_STATUS) response[4] = 1;
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.saveSettings()).toBeFalse();
  });

  test("accepts only version zero as the legacy SAVE_SETTINGS response", async () => {
    let protocolVersion = 0;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        const response = new Uint8Array(4);
        response.set([command, Status.OK, 0, protocolVersion]);
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.saveSettings()).toBeTrue();
    protocolVersion = 2;
    expect(await device.saveSettings()).toBeFalse();
  });

  test("drains an owned settings transaction before RAM-only mutations", async () => {
    const calls: number[] = [];
    let statusReads = 0;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = 0;
        if (command === Command.GET_SETTINGS_SAVE_STATUS) {
          response[2] = SettingsSaveState.RamOnly;
          response[3] = 1;
          response[4] = 1;
          response[6] = statusReads++ === 0 ? 1 : 0;
        }
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.enterRamOnlyMode()).toBeTrue();
    expect(calls).toEqual([
      Command.SET_RAM_ONLY_MODE,
      Command.GET_SETTINGS_SAVE_STATUS,
      Command.GET_SETTINGS_SAVE_STATUS,
    ]);
  });

  test("keeps legacy RAM-only entry compatible without save-status support", async () => {
    const calls: number[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        const response = new Uint8Array(64);
        response[0] = command;
        response[1] = command === Command.SET_RAM_ONLY_MODE ? Status.OK : Status.INVALID_CMD;
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.enterRamOnlyMode()).toBeTrue();
    expect(calls).toEqual([
      Command.SET_RAM_ONLY_MODE,
      Command.GET_SETTINGS_SAVE_STATUS,
    ]);
  });

  test("does not treat a missing save-status response as legacy support", async () => {
    const calls: number[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        if (command === Command.GET_SETTINGS_SAVE_STATUS) return null;
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.enterRamOnlyMode()).toBeFalse();
    expect(calls).toEqual([
      Command.SET_RAM_ONLY_MODE,
      Command.GET_SETTINGS_SAVE_STATUS,
      Command.GET_SETTINGS_SAVE_STATUS,
    ]);
  });

  test("does not treat an explicit transient status error as legacy support", async () => {
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => Uint8Array.from([
        command,
        command === Command.SET_RAM_ONLY_MODE ? Status.OK : Status.ERROR,
      ]),
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.enterRamOnlyMode()).toBeFalse();
  });

  test("recovers a transient missing save-status response through the safe GET retry", async () => {
    let statusAttempts = 0;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        if (command !== Command.GET_SETTINGS_SAVE_STATUS) {
          return Uint8Array.from([command, Status.OK]);
        }
        if (statusAttempts++ === 0) return null;
        const response = new Uint8Array(7);
        response.set([
          command,
          Status.OK,
          SettingsSaveState.RamOnly,
          1,
          1,
          0,
          0,
        ]);
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.enterRamOnlyMode()).toBeTrue();
    expect(statusAttempts).toBe(2);
  });

  test("propagates a save-status transport error instead of classifying it as legacy", async () => {
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        if (command === Command.GET_SETTINGS_SAVE_STATUS) {
          throw new Error("transport offline");
        }
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    await expect(device.enterRamOnlyMode()).rejects.toThrow("transport offline");
  });
});

describe("KBHEDevice action capabilities", () => {
  test("exposes ProfileDocument and runtime-state extension capability bytes", async () => {
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        const response = new Uint8Array(64);
        response.set([command, 0, 1, 4, 16, 32, 16, 8, 4, 4, 3, 1, 0x03]);
        return response;
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getActionCapabilities()).toMatchObject({
      profileDocumentSchemaVersion: 3,
      atomicProfileDocumentCommit: true,
      maxInstances: 4,
      runtimeStateCommand: true,
      extendedStateReport: true,
    });
  });

  test("decodes runtime/default bits and bounded queue telemetry separately", async () => {
    const response = new Uint8Array(64);
    response.set([
      Command.GET_ACTION_STATES,
      Status.OK,
      0x05,
      0x00,
      2,
      1,
      0x02,
      0x00,
      3,
      7,
      16,
      0x78,
      0x56,
      0x34,
      0x12,
    ]);
    const transport = {
      sendCommand: async () => response,
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getActionStates()).toEqual({
      bits: 0x0005,
      initialBits: 0x0002,
      activeProfileIndex: 2,
      metricsAvailable: true,
      activeInstances: 3,
      pendingTriggers: 7,
      triggerQueueCapacity: 16,
      droppedTriggers: 0x12345678,
    });
  });

  test("falls back safely when old firmware only reports live state bits", async () => {
    const response = new Uint8Array(64);
    response.set([Command.GET_ACTION_STATES, Status.OK, 0x34, 0x12, 1]);
    const transport = {
      sendCommand: async () => response,
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.getActionStates()).toEqual({
      bits: 0x1234,
      initialBits: 0x1234,
      activeProfileIndex: 1,
      metricsAvailable: false,
      activeInstances: 0,
      pendingTriggers: 0,
      triggerQueueCapacity: 0,
      droppedTriggers: 0,
    });
  });

  test("uses the runtime-only command and verifies its echoed postcondition", async () => {
    let capturedCommand = 0;
    let capturedData: number[] = [];
    const transport = {
      sendCommand: async (command: number, data: ArrayLike<number>) => {
        capturedCommand = command;
        capturedData = Array.from(data);
        return Uint8Array.from([command, Status.OK, 6, 1]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.setActionRuntimeState(6, true)).toBeTrue();
    expect(capturedCommand).toBe(Command.SET_ACTION_RUNTIME_STATE);
    expect(capturedData).toEqual([2, 6, 1]);
    capturedCommand = 0;
    expect(await device.setActionRuntimeState(16, true)).toBeFalse();
    expect(capturedCommand).toBe(0);
  });
});

describe("KBHEDevice durable action mutations", () => {
  test("allows delayed program commits without replaying the persistent command", async () => {
    const profileIndex = 2;
    const programIndex = 5;
    const program = {
      version: 1 as const,
      flags: 0,
      steps: [{ opcode: ActionOpcode.End, arg8: 0, arg16: 0 }],
    };
    const hash = actionProgramHash(program);
    const simulatedResponseDelayMs = 1_500;
    const calls: Array<{ command: number; timeoutMs: number }> = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (
        command: number,
        _data: ArrayLike<number>,
        timeoutMs: number,
      ) => {
        calls.push({ command, timeoutMs });
        if (command === Command.BEGIN_SET_ACTION_PROGRAM) {
          return Uint8Array.from([command, Status.OK, profileIndex, programIndex]);
        }
        if (command === Command.SET_ACTION_PROGRAM_CHUNK) {
          return Uint8Array.from([command, Status.OK, profileIndex, programIndex, 0, 1]);
        }
        if (command === Command.COMMIT_ACTION_PROGRAM) {
          if (timeoutMs < simulatedResponseDelayMs) return null;
          return Uint8Array.from([
            command,
            Status.OK,
            profileIndex,
            programIndex,
            hash & 0xff,
            (hash >>> 8) & 0xff,
            (hash >>> 16) & 0xff,
            (hash >>> 24) & 0xff,
          ]);
        }
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.setActionProgram(profileIndex, programIndex, program)).toBeTrue();
    expect(calls.filter(({ command }) => command === Command.COMMIT_ACTION_PROGRAM))
      .toEqual([{ command: Command.COMMIT_ACTION_PROGRAM, timeoutMs: 5_000 }]);
  });

  test("reconciles a lost commit response through metadata without replay or abort", async () => {
    const profileIndex = 1;
    const programIndex = 3;
    const program = {
      version: 1 as const,
      flags: 0,
      steps: [{ opcode: ActionOpcode.End, arg8: 0, arg16: 0 }],
    };
    const hash = actionProgramHash(program);
    const calls: number[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        if (command === Command.BEGIN_SET_ACTION_PROGRAM) {
          return Uint8Array.from([command, Status.OK, profileIndex, programIndex]);
        }
        if (command === Command.SET_ACTION_PROGRAM_CHUNK) {
          return Uint8Array.from([command, Status.OK, profileIndex, programIndex, 0, 1]);
        }
        if (command === Command.COMMIT_ACTION_PROGRAM) return null;
        if (command === Command.GET_ACTION_PROGRAM_META) {
          return Uint8Array.from([
            command,
            Status.OK,
            profileIndex,
            programIndex,
            program.version,
            program.flags,
            program.steps.length,
            0,
            hash & 0xff,
            (hash >>> 8) & 0xff,
            (hash >>> 16) & 0xff,
            (hash >>> 24) & 0xff,
          ]);
        }
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.setActionProgram(profileIndex, programIndex, program)).toBeTrue();
    expect(calls.filter((command) => command === Command.COMMIT_ACTION_PROGRAM)).toHaveLength(1);
    expect(calls).toContain(Command.GET_ACTION_PROGRAM_META);
    expect(calls).not.toContain(Command.ABORT_ACTION_PROGRAM);
  });

  test("allows delayed overlay acknowledgements with the persistent timeout", async () => {
    const profileIndex = 3;
    const overlayIndex = 6;
    const binding = { ...defaultActionOverlayBinding(), enabled: true };
    const simulatedResponseDelayMs = 1_500;
    const calls: Array<{ command: number; timeoutMs: number }> = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (
        command: number,
        _data: ArrayLike<number>,
        timeoutMs: number,
      ) => {
        calls.push({ command, timeoutMs });
        if (command !== Command.SET_ACTION_OVERLAY || timeoutMs < simulatedResponseDelayMs) {
          return null;
        }
        return Uint8Array.from([command, Status.OK, profileIndex, overlayIndex]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.setActionOverlay(profileIndex, overlayIndex, binding)).toBeTrue();
    expect(calls).toEqual([{
      command: Command.SET_ACTION_OVERLAY,
      timeoutMs: 5_000,
    }]);
  });

  test("reconciles a lost overlay response without replaying SET_ACTION_OVERLAY", async () => {
    const profileIndex = 0;
    const overlayIndex = 4;
    const binding = {
      ...defaultActionOverlayBinding(),
      enabled: true,
      allKeys: false,
      color: [12, 34, 56] as [number, number, number],
      keyMask: [1, 2, 4, 8, 16, 32, 64, 128, 3, 5, 9],
    };
    const calls: number[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (command: number) => {
        calls.push(command);
        if (command === Command.SET_ACTION_OVERLAY) return null;
        if (command === Command.GET_ACTION_OVERLAY) {
          return actionOverlayGetResponse(profileIndex, overlayIndex, binding);
        }
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.setActionOverlay(profileIndex, overlayIndex, binding)).toBeTrue();
    expect(calls.filter((command) => command === Command.SET_ACTION_OVERLAY)).toHaveLength(1);
    expect(calls).toContain(Command.GET_ACTION_OVERLAY);
  });
});

describe("KBHEDevice RAM-only exit", () => {
  test("uses the O(1) leave extension without reloading settings", async () => {
    let capturedData: number[] = [];
    let capturedTimeout = 0;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async (
        command: number,
        data: ArrayLike<number>,
        timeoutMs: number,
      ) => {
        expect(command).toBe(Command.SET_RAM_ONLY_MODE);
        capturedData = Array.from(data);
        capturedTimeout = timeoutMs;
        return Uint8Array.from([command, Status.OK, 0]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.leaveRamOnlyMode()).toBeTrue();
    expect(capturedData).toEqual([2, 0, 1]);
    expect(capturedTimeout).toBe(300);
  });

  test("rejects an invalid or internally inconsistent RAM-only leave response", async () => {
    let response = Uint8Array.from([
      Command.SET_RAM_ONLY_MODE,
      Status.INVALID_PARAM,
      1,
    ]);
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      sendCommand: async () => response,
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.leaveRamOnlyMode()).toBeFalse();
    response = Uint8Array.from([Command.SET_RAM_ONLY_MODE, Status.OK, 1]);
    expect(await device.leaveRamOnlyMode()).toBeFalse();
  });

  test("reconnects the same serial-numbered keyboard after the reload reboot", async () => {
    const original = runtimeDevice("runtime-old", "SERIAL-A");
    const reappeared = runtimeDevice("runtime-new", "SERIAL-A");
    const unrelated = runtimeDevice("runtime-other", "SERIAL-B");
    const events: string[] = [];
    let restarting = false;
    let connectedPath: string | null = original.path;
    let connectArgs: [string, string | undefined] | null = null;
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      connectionState: async () => ({
        connected: connectedPath !== null,
        path: connectedPath,
        pid: 0x4004,
        kind: connectedPath === null ? null : "runtime",
      }),
      listDevices: async () => restarting ? [unrelated, reappeared] : [original, unrelated],
      disconnect: async () => {
        events.push("disconnect");
        connectedPath = null;
        return { connected: false, path: null, pid: null, kind: null };
      },
      connect: async (path: string, expectedSerialNumber?: string) => {
        events.push(`connect:${path}`);
        connectedPath = path;
        connectArgs = [path, expectedSerialNumber];
        return { connected: true, path, pid: 0x4004, kind: "runtime" };
      },
      sendCommand: async (command: number) => {
        expect(command).toBe(Command.RELOAD_SETTINGS_FROM_FLASH);
        events.push("reload");
        restarting = true;
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.exitRamOnlyMode()).toBeTrue();
    expect(events).toEqual(["reload", "disconnect", `connect:${reappeared.path}`]);
    expect(connectArgs).toEqual([reappeared.path, "SERIAL-A"]);
  });

  test("falls back to the original path, never the first arbitrary candidate", async () => {
    const original = runtimeDevice("stable-path", null);
    const unrelated = runtimeDevice("other-path", null);
    let restarting = false;
    let connectedPath: string | null = original.path;
    const connectedPaths: string[] = [];
    const transport = {
      flushInput: async () => 0,
      writeReport: async () => 65,
      readReport: async () => new Uint8Array(),
      connectionState: async () => ({
        connected: connectedPath !== null,
        path: connectedPath,
        pid: 0x4004,
        kind: connectedPath === null ? null : "runtime",
      }),
      listDevices: async () => restarting ? [unrelated, original] : [original, unrelated],
      disconnect: async () => {
        connectedPath = null;
        return { connected: false, path: null, pid: null, kind: null };
      },
      connect: async (path: string) => {
        connectedPaths.push(path);
        connectedPath = path;
        return { connected: true, path, pid: 0x4004, kind: "runtime" };
      },
      sendCommand: async (command: number) => {
        restarting = true;
        return Uint8Array.from([command, Status.OK]);
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.exitRamOnlyMode()).toBeTrue();
    expect(connectedPaths).toEqual([original.path]);
  });
});

describe("KBHEDevice reconnect identity", () => {
  test("reconnects the same serial after its HID path changes", async () => {
    const original = runtimeDevice("runtime-old", "SERIAL-A");
    const reappeared = runtimeDevice("runtime-new", "SERIAL-A");
    const unrelated = runtimeDevice("runtime-other", "SERIAL-B");
    let connectedPath: string | null = original.path;
    let enumeration = 0;
    let connectArgs: [string, string | undefined] | null = null;
    const transport = {
      listDevices: async () => ++enumeration === 1
        ? [original, unrelated]
        : [unrelated, reappeared],
      connectionState: async () => ({
        connected: connectedPath !== null,
        path: connectedPath,
        pid: connectedPath === null ? null : 0x4004,
        kind: connectedPath === null ? null : "runtime",
      }),
      disconnect: async () => {
        connectedPath = null;
        return { connected: false, path: null, pid: null, kind: null };
      },
      connect: async (path: string, expectedSerialNumber?: string) => {
        connectedPath = path;
        connectArgs = [path, expectedSerialNumber];
        return { connected: true, path, pid: 0x4004, kind: "runtime" };
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    expect(await device.reconnect()).toBeTrue();
    expect(connectArgs).toEqual([reappeared.path, "SERIAL-A"]);
  });

  test("refuses an ambiguous replacement instead of reconnecting the first match", async () => {
    const original = runtimeDevice("runtime-old", "SERIAL-A");
    const duplicateA = runtimeDevice("runtime-new-a", "SERIAL-A");
    const duplicateB = runtimeDevice("runtime-new-b", "SERIAL-A");
    let connectedPath: string | null = original.path;
    let enumeration = 0;
    let connectCalled = false;
    const transport = {
      listDevices: async () => ++enumeration === 1
        ? [original]
        : [duplicateA, duplicateB],
      connectionState: async () => ({
        connected: connectedPath !== null,
        path: connectedPath,
        pid: connectedPath === null ? null : 0x4004,
        kind: connectedPath === null ? null : "runtime",
      }),
      disconnect: async () => {
        connectedPath = null;
        return { connected: false, path: null, pid: null, kind: null };
      },
      connect: async () => {
        connectCalled = true;
        throw new Error("must not connect");
      },
    } as KbheTransport;
    const device = new KBHEDevice(new KbheCommander(transport), transport);

    let error: unknown = null;
    try {
      await device.reconnect();
    } catch (caught) {
      error = caught;
    }
    expect(error).toBeInstanceOf(Error);
    expect((error as Error).message).toContain("ambiguous KBHE target");
    expect(connectCalled).toBeFalse();
  });
});
