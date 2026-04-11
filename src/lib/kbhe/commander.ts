import { buildCommandReport, Command, PACKET_SIZE } from "./protocol";
import { kbheTransport, type KbheTransport } from "./transport";

export class KbheCommander {
  private queue: Promise<unknown> = Promise.resolve();

  constructor(private readonly transport: KbheTransport = kbheTransport) {}

  enqueue<T>(task: () => Promise<T>): Promise<T> {
    const next = this.queue.then(task, task);
    this.queue = next.then(
      () => undefined,
      () => undefined,
    );
    return next;
  }

  async sendCommand(
    command: Command | number,
    data: ArrayLike<number> = [],
    timeoutMs = 100,
  ): Promise<Uint8Array | null> {
    return this.enqueue(async () => {
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
