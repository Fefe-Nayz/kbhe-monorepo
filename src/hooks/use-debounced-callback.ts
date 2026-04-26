import { useCallback, useRef } from "react";

/**
 * Returns a debounced version of the callback that delays invocation
 * until `ms` milliseconds after the last call.
 * Useful for slider onValueChange to avoid spamming device writes on every frame.
 */
export function useDebouncedCallback<T extends (...args: never[]) => void>(
  fn: T,
  ms: number,
): T {
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const debounced = useCallback(
    (...args: Parameters<T>) => {
      if (timer.current) clearTimeout(timer.current);
      timer.current = setTimeout(() => fn(...args), ms);
    },
    [fn, ms],
  );
  return debounced as T;
}
