import { buildCommandReport, Command, PACKET_SIZE } from "./protocol";
import { kbheTransport, type KbheTransport } from "./transport";

export function isRetrySafeCommand(command: Command | number): boolean {
  const name = Command[command as Command];
  return name?.startsWith("GET_") === true
    || command === Command.ECHO
    || command === Command.GUIDED_CALIBRATION_STATUS;
}

export function isAtomicCommandUnavailableError(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /kbhe_send_command/i.test(message)
    && /(not found|unknown|does not exist|not registered)/i.test(message);
}

export class KbheCommander {
  private queue: Promise<unknown> = Promise.resolve();
  private atomicCommandAvailable: boolean | null = null;

  constructor(private readonly transport: KbheTransport = kbheTransport) {}

  resetTransportCapabilities(): void {
    this.atomicCommandAvailable = null;
  }

  enqueue<T>(task: () => Promise<T>): Promise<T> {
    const next = this.queue.then(task, task);
    this.queue = next.then(
      () => undefined,
      () => undefined,
    );
    return next;
  }

  /**
   * Drain the transport queue, including follow-up commands enqueued by the
   * completion continuation of an earlier command. Profile switches and USB
   * teardown use this as a strict ordering barrier.
   */
  async waitForIdle(): Promise<void> {
    for (;;) {
      const observedTail = this.queue;
      await observedTail;
      await new Promise((resolve) => setTimeout(resolve, 0));
      if (observedTail === this.queue) return;
    }
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
      } catch (error) {
        if (!isAtomicCommandUnavailableError(error)) {
          throw error;
        }

        // Older backend with no atomic invoke handler: use legacy path.
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
    if (data.length > PACKET_SIZE - 1) {
      throw new RangeError(
        `RAW HID command payload has ${data.length} bytes; maximum is ${PACKET_SIZE - 1}`,
      );
    }
    return this.enqueue(async () => {
      const maxAttempts = isRetrySafeCommand(command) ? 2 : 1;

      for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
        const response = await this.sendCommandAtomicOrLegacy(command, data, timeoutMs);
        if (response && response.length >= 2 && response[0] === (command & 0xff)) {
          return response;
        }

        if (attempt + 1 < maxAttempts) {
          await new Promise((resolve) => setTimeout(resolve, 4));
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
