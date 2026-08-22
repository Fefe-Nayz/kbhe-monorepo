import { describe, expect, test } from "bun:test";

import { createLatestWinsDispatcher } from "./use-throttled-call";

function deferred() {
  let resolve!: () => void;
  const promise = new Promise<void>((done) => {
    resolve = done;
  });
  return { promise, resolve };
}

describe("latest-wins live dispatcher", () => {
  test("keeps only the most recent queued preview", async () => {
    const first = deferred();
    const values: number[] = [];
    const dispatcher = createLatestWinsDispatcher(async (value: number) => {
      values.push(value);
      if (value === 1) await first.promise;
    });

    dispatcher(1);
    dispatcher(2);
    dispatcher(3);
    first.resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));

    expect(values).toEqual([1, 3]);
  });

  test("drops a stale queued preview before a final commit barrier", async () => {
    const first = deferred();
    const values: number[] = [];
    const dispatcher = createLatestWinsDispatcher(async (value: number) => {
      values.push(value);
      if (value === 1) await first.promise;
    });

    dispatcher(1);
    dispatcher(2);
    dispatcher.cancelPending();
    first.resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));

    expect(values).toEqual([1]);
  });

  test("commit barrier waits for the in-flight preview to settle", async () => {
    const first = deferred();
    const dispatcher = createLatestWinsDispatcher(async () => first.promise);
    dispatcher(1);
    dispatcher(2);

    let drained = false;
    const barrier = dispatcher.cancelAndWait().then(() => {
      drained = true;
    });
    await Promise.resolve();
    expect(drained).toBe(false);

    first.resolve();
    await barrier;
    expect(drained).toBe(true);
  });

  test("does not start queued work after disposal", async () => {
    const first = deferred();
    const values: number[] = [];
    const dispatcher = createLatestWinsDispatcher(async (value: number) => {
      values.push(value);
      if (value === 1) await first.promise;
    });

    dispatcher(1);
    dispatcher(2);
    dispatcher.dispose();
    first.resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));

    expect(values).toEqual([1]);
  });
});
