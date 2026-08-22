import {
  cancelAllThrottledCalls,
  cancelAndDrainAllThrottledCalls,
} from "@/hooks/use-throttled-call";
import { kbheCommander } from "@/lib/kbhe/commander";

const profileOperationTokenBrand = Symbol("profile-operation-token");

export interface ProfileOperationToken {
  readonly [profileOperationTokenBrand]: true;
}

let operationTail: Promise<void> = Promise.resolve();
let pendingOperations = 0;
const pendingListeners = new Set<() => void>();

function notifyPendingListeners(): void {
  for (const listener of pendingListeners) {
    listener();
  }
}

function incrementPendingOperations(): void {
  pendingOperations += 1;
  notifyPendingListeners();
}

function decrementPendingOperations(): void {
  pendingOperations = Math.max(0, pendingOperations - 1);
  notifyPendingListeners();
}

/**
 * Keep the global profile interaction barrier active around a multi-step UI
 * workflow that may call several individually locked helpers.
 */
export function acquireProfileInteractionLease(): () => void {
  incrementPendingOperations();
  let released = false;
  return () => {
    if (released) return;
    released = true;
    decrementPendingOperations();
  };
}

export function getProfileOperationPending(): boolean {
  return pendingOperations > 0;
}

export function subscribeProfileOperationPending(listener: () => void): () => void {
  pendingListeners.add(listener);
  return () => pendingListeners.delete(listener);
}

export function isProfileOperationToken(value: unknown): value is ProfileOperationToken {
  return Boolean(value)
    && typeof value === "object"
    && (value as Partial<ProfileOperationToken>)[profileOperationTokenBrand] === true;
}

export function runProfileOperation<T>(
  operation: (token: ProfileOperationToken) => Promise<T>,
): Promise<T> {
  // Prevent an interaction preview captured for the old active profile from
  // entering the HID queue after this profile operation.
  cancelAllThrottledCalls();
  incrementPendingOperations();
  const token = { [profileOperationTokenBrand]: true } as ProfileOperationToken;
  const result = operationTail.then(
    async () => {
      await cancelAndDrainAllThrottledCalls();
      await kbheCommander.waitForIdle();
      return operation(token);
    },
    async () => {
      await cancelAndDrainAllThrottledCalls();
      await kbheCommander.waitForIdle();
      return operation(token);
    },
  );
  operationTail = result.then(
    () => undefined,
    () => undefined,
  );
  void result.then(
    () => {
      decrementPendingOperations();
    },
    () => {
      decrementPendingOperations();
    },
  );
  return result;
}
