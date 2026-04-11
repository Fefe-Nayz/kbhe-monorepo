import { useSyncExternalStore } from "react";

function subscribe(callback: () => void) {
  document.addEventListener("visibilitychange", callback);
  return () => document.removeEventListener("visibilitychange", callback);
}

function getSnapshot() {
  return document.visibilityState === "visible";
}

function getServerSnapshot() {
  return true;
}

/**
 * Returns true when the browser tab/window is visible.
 * Use this to pause polling when the app is backgrounded.
 */
export function usePageVisible(): boolean {
  return useSyncExternalStore(subscribe, getSnapshot, getServerSnapshot);
}
