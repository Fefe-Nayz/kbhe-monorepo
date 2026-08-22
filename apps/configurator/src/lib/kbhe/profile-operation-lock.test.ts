import { expect, test } from "bun:test";

import {
  acquireProfileInteractionLease,
  getProfileOperationPending,
  runProfileOperation,
  subscribeProfileOperationPending,
} from "./profile-operation-lock";

test("profile operations execute serially even after an earlier rejection", async () => {
  const events: string[] = [];
  const first = runProfileOperation(async () => {
    events.push("first:start");
    await Promise.resolve();
    events.push("first:end");
    throw new Error("expected");
  });
  const second = runProfileOperation(async () => {
    events.push("second:start");
    events.push("second:end");
  });

  await expect(first).rejects.toThrow("expected");
  await second;
  expect(events).toEqual(["first:start", "first:end", "second:start", "second:end"]);
});

test("reports queued profile work as pending until the last operation settles", async () => {
  const states: boolean[] = [];
  let releaseFirst!: () => void;
  let releaseSecond!: () => void;
  const firstGate = new Promise<void>((resolve) => { releaseFirst = resolve; });
  const secondGate = new Promise<void>((resolve) => { releaseSecond = resolve; });
  const unsubscribe = subscribeProfileOperationPending(() => {
    states.push(getProfileOperationPending());
  });

  const first = runProfileOperation(() => firstGate);
  const second = runProfileOperation(() => secondGate);
  expect(getProfileOperationPending()).toBeTrue();
  releaseFirst();
  await first;
  expect(getProfileOperationPending()).toBeTrue();
  releaseSecond();
  await second;
  expect(getProfileOperationPending()).toBeFalse();
  unsubscribe();
  expect(states).toEqual([true, true, true, false]);
});

test("keeps a multi-step interaction lease pending and releases it only once", () => {
  const release = acquireProfileInteractionLease();
  expect(getProfileOperationPending()).toBeTrue();
  release();
  release();
  expect(getProfileOperationPending()).toBeFalse();
});
