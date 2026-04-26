import { buildCommandReport, Command, PACKET_SIZE } from "./protocol";
import { kbheTransport, type KbheTransport } from "./transport";

export class KbheCommander {
  private queue: Promise<unknown> = Promise.resolve();
  private atomicCommandAvailable: boolean | null = null;

  constructor(private readonly transport: KbheTransport = kbheTransport) {}

  enqueue<T>(task: () => Promise<T>): Promise<T> {
    const next = this.queue.then(task, task);
    this.queue = next.then(
      () => undefined,
      () => undefined,
    );
    return next;
  }

  private async sendCommandLegacy(
    command: Command | number,
    data: ArrayLike<number>,
    timeoutMs: number,
  ): Promise<Uint8Array | null> {
    await this.transport.flushInput();

    const report = buildCommandReport(command, data);
    await this.transport.writeReport(report);

    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
      const remaining = Math.max(1, Math.ceil(deadline - performance.now()));
      const response = await this.transport.readReport(remaining);
      if (response.length >= 2 && response[0] === (command & 0xff)) {
        return response;
      }
    }

    return null;
  }

  private async sendCommandAtomicOrLegacy(
    command: Command | number,
    data: ArrayLike<number>,
    timeoutMs: number,
  ): Promise<Uint8Array | null> {
    if (this.atomicCommandAvailable !== false) {
      try {
        const response = await this.transport.sendCommand(command, data, timeoutMs);
        this.atomicCommandAvailable = true;
        return response;
      } catch {
        // Older backend or missing invoke handler: use legacy path.
        this.atomicCommandAvailable = false;
      }
    }

    return this.sendCommandLegacy(command, data, timeoutMs);
  }

  async sendCommand(
    command: Command | number,
    data: ArrayLike<number> = [],
    timeoutMs = 100,
  ): Promise<Uint8Array | null> {
    return this.enqueue(async () => {
      const maxAttempts = 2;

      for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
        const response = await this.sendCommandAtomicOrLegacy(command, data, timeoutMs);
        if (response && response.length >= 2 && response[0] === (command & 0xff)) {
          return response;
        }

        if (attempt + 1 < maxAttempts) {
          await new Promise((resolve) => window.setTimeout(resolve, 4));
        }
      }

      return null;
    });
  }

  async sendRawReport(report: ArrayLike<number>, timeoutMs = 100): Promise<Uint8Array> {
    return this.enqueue(async () => {
      await this.transport.writeReport(report);
      return this.transport.readReport(timeoutMs);
    });
  }

  packetSize(): number {
    return PACKET_SIZE;
  }
}

export const kbheCommander = new KbheCommander();
