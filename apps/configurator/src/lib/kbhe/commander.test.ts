import { describe, expect, test } from "bun:test";

import { KbheCommander } from "./commander";
import { Command } from "./protocol";
import type { KbheTransport } from "./transport";

function mockTransport(overrides: Partial<KbheTransport> = {}): KbheTransport {
  return {
    flushInput: async () => 0,
    writeReport: async () => 65,
    readReport: async () => new Uint8Array(),
    sendCommand: async () => null,
    ...overrides,
  } as KbheTransport;
}

describe("KbheCommander retry and fallback policy", () => {
  test("does not replay a non-idempotent command after a lost response", async () => {
    let attempts = 0;
    const commander = new KbheCommander(mockTransport({
      sendCommand: async () => {
        attempts += 1;
        return null;
      },
    }));

    expect(await commander.sendCommand(Command.CREATE_PROFILE)).toBeNull();
    expect(attempts).toBe(1);
  });

  test("retries a safe GET command once", async () => {
    let attempts = 0;
    const commander = new KbheCommander(mockTransport({
      sendCommand: async (command) => {
        attempts += 1;
        return attempts === 1 ? null : Uint8Array.from([command, 0]);
      },
    }));

    expect(await commander.sendCommand(Command.GET_OPTIONS)).toEqual(Uint8Array.from([Command.GET_OPTIONS, 0]));
    expect(attempts).toBe(2);
  });

  test("does not permanently downgrade on a transient atomic transport error", async () => {
    let legacyWrites = 0;
    const commander = new KbheCommander(mockTransport({
      sendCommand: async () => { throw new Error("device disconnected"); },
      writeReport: async () => {
        legacyWrites += 1;
        return 65;
      },
    }));

    await expect(commander.sendCommand(Command.GET_OPTIONS)).rejects.toThrow("device disconnected");
    expect(legacyWrites).toBe(0);
  });

  test("uses the compatibility path only when the atomic handler is absent", async () => {
    let legacyWrites = 0;
    const commander = new KbheCommander(mockTransport({
      sendCommand: async () => { throw new Error("Command kbhe_send_command not found"); },
      writeReport: async () => {
        legacyWrites += 1;
        return 65;
      },
      readReport: async () => Uint8Array.from([Command.GET_OPTIONS, 0]),
    }));

    expect(await commander.sendCommand(Command.GET_OPTIONS)).toEqual(Uint8Array.from([Command.GET_OPTIONS, 0]));
    expect(legacyWrites).toBe(1);
  });

  test("rejects an oversized payload instead of silently truncating a mutation", async () => {
    const commander = new KbheCommander(mockTransport({
      sendCommand: async () => { throw new Error("Command kbhe_send_command not found"); },
    }));

    await expect(commander.sendCommand(Command.SET_OPTIONS, new Uint8Array(64)))
      .rejects.toThrow("maximum is 63");
  });

  test("idle barrier follows a command enqueued by an earlier completion", async () => {
    const commands: number[] = [];
    const commander = new KbheCommander(mockTransport({
      sendCommand: async (command) => {
        commands.push(command);
        return Uint8Array.from([command, 0]);
      },
    }));

    const first = commander.sendCommand(Command.GET_OPTIONS);
    void first.then(() => commander.sendCommand(Command.GET_LED_ENABLED));
    await commander.waitForIdle();

    expect(commands).toEqual([Command.GET_OPTIONS, Command.GET_LED_ENABLED]);
  });
});
