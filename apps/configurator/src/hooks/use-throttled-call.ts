import { useEffect, useState } from "react";

export interface ThrottledCall<T> {
  (value: T): void;
  /** Drop a queued preview value without attempting to cancel the in-flight write. */
  cancelPending: () => void;
  /** Drop queued work and resolve after the current transport write settles. */
  cancelAndWait: () => Promise<void>;
  /** Resolve once both the in-flight and queued preview work are empty. */
  waitForIdle: () => Promise<void>;
}

interface LatestWinsDispatcher<T> extends ThrottledCall<T> {
  dispose: () => void;
  setHandler: (handler: (value: T) => Promise<void>) => void;
}

const activeDispatchers = new Set<LatestWinsDispatcher<unknown>>();

/**
 * Profile switches and disconnects are transport barriers. A preview captured
 * for the previous runtime context must never be replayed after that barrier.
 */
export function cancelAllThrottledCalls(): void {
  for (const dispatcher of activeDispatchers) {
    dispatcher.cancelPending();
  }
}

export async function cancelAndDrainAllThrottledCalls(): Promise<void> {
  const dispatchers = [...activeDispatchers];
  for (const dispatcher of dispatchers) {
    dispatcher.cancelPending();
  }
  await Promise.all(dispatchers.map((dispatcher) => dispatcher.waitForIdle()));
}

export function createLatestWinsDispatcher<T>(
  fn: (value: T) => Promise<void>,
): LatestWinsDispatcher<T> {
  let handler = fn;
  let inFlight = false;
  let pending: T | undefined;
  let hasPending = false;
  let disposed = false;
  let idleWaiters: Array<() => void> = [];

  const resolveIdleWaiters = () => {
    if (inFlight || hasPending) return;
    const waiters = idleWaiters;
    idleWaiters = [];
    for (const resolve of waiters) resolve();
  };

  const cancelPending = () => {
    hasPending = false;
    pending = undefined;
    resolveIdleWaiters();
  };

  const waitForIdle = (): Promise<void> => {
    if (!inFlight && !hasPending) return Promise.resolve();
    return new Promise((resolve) => idleWaiters.push(resolve));
  };

  const fire = async (value: T): Promise<void> => {
    if (disposed) return;
    inFlight = true;
    hasPending = false;
    try {
      await handler(value);
    } catch {
      // Live previews are best effort. The final commit surfaces failures.
    } finally {
      inFlight = false;
      if (!disposed && hasPending) {
        const next = pending as T;
        hasPending = false;
        pending = undefined;
        void fire(next);
      } else {
        resolveIdleWaiters();
      }
    }
  };

  const dispatch = ((value: T) => {
    if (disposed) return;
    if (inFlight) {
      pending = value;
      hasPending = true;
      return;
    }
    void fire(value);
  }) as LatestWinsDispatcher<T>;

  dispatch.cancelPending = cancelPending;
  dispatch.waitForIdle = waitForIdle;
  dispatch.cancelAndWait = () => {
    cancelPending();
    return waitForIdle();
  };
  dispatch.setHandler = (nextHandler) => {
    handler = nextHandler;
  };
  dispatch.dispose = () => {
    disposed = true;
    cancelPending();
    resolveIdleWaiters();
  };
  return dispatch;
}

/**
 * Returns a stable dispatcher that executes `fn` with "latest-wins" throttling:
 * - If no call is in flight → fires immediately
 * - If a call is in flight → stores the latest value; fires it once the
 *   current call settles (only the most-recent pending value is kept)
 *
 * Designed for live HID device updates during slider/curve drag:
 * max 1 concurrent request, zero dropped final values.
 */
export function useThrottledCall<T>(
  fn: (value: T) => Promise<void>,
): ThrottledCall<T> {
  const [dispatcher] = useState(() => createLatestWinsDispatcher<T>(fn));

  useEffect(() => {
    dispatcher.setHandler(fn);
  }, [dispatcher, fn]);

  useEffect(() => {
    activeDispatchers.add(dispatcher as LatestWinsDispatcher<unknown>);
    return () => {
      activeDispatchers.delete(dispatcher as LatestWinsDispatcher<unknown>);
      dispatcher.dispose();
    };
  }, [dispatcher]);

  return dispatcher;
}
