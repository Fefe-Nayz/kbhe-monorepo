import { useRef, useCallback } from "react";

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
): (value: T) => void {
  // Always call the latest fn so closures over props/state stay fresh.
  const fnRef = useRef(fn);
  fnRef.current = fn;

  const inFlightRef = useRef(false);
  const pendingRef = useRef<T | undefined>(undefined);
  const hasPendingRef = useRef(false);

  const fire = useCallback(async (value: T) => {
    inFlightRef.current = true;
    hasPendingRef.current = false;
    try {
      await fnRef.current(value);
    } catch {
      // Silently swallow errors from live preview calls — the commit
      // mutation (on pointer-up) will surface errors properly.
    } finally {
      inFlightRef.current = false;
      if (hasPendingRef.current) {
        const next = pendingRef.current as T;
        hasPendingRef.current = false;
        pendingRef.current = undefined;
        void fire(next);
      }
    }
  }, []);

  return useCallback(
    (value: T) => {
      if (inFlightRef.current) {
        pendingRef.current = value;
        hasPendingRef.current = true;
        return;
      }
      void fire(value);
    },
    [fire],
  );
}
