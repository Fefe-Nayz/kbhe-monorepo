import { useState, useRef, useCallback, useEffect } from "react";
import { toast } from "sonner";

export type SaveState = "idle" | "saving" | "saved" | "error";

interface Props {
  state: SaveState;
  errorMessage?: string;
  className?: string;
}

export function AutosaveStatus(_props: Props) {
  void _props;
  // Status is now surfaced through Sonner toasts instead of inline text badges.
  return null;
}

interface PendingSaveToast {
  toastRef: ReturnType<typeof toast.promise>;
  resolve: () => void;
  reject: (reason?: unknown) => void;
}

function toErrorMessage(reason: unknown): string {
  if (typeof reason === "string" && reason.trim()) return reason;
  if (reason instanceof Error && reason.message.trim()) return reason.message;
  return "Save failed";
}

export function useAutosave() {
  const [saveState, setSaveState] = useState<SaveState>("idle");
  const clearTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingToast = useRef<PendingSaveToast | null>(null);

  const startPromiseToast = useCallback(() => {
    if (pendingToast.current) {
      return;
    }

    let resolve!: () => void;
    let reject!: (reason?: unknown) => void;
    const promise = new Promise<void>((res, rej) => {
      resolve = res;
      reject = rej;
    });

    const toastRef = toast.promise(promise, {
      loading: "Saving...",
      success: "Saved",
      error: (reason) => toErrorMessage(reason),
    });

    pendingToast.current = { toastRef, resolve, reject };
  }, []);

  const markSaving = useCallback(() => {
    if (clearTimer.current) clearTimeout(clearTimer.current);
    setSaveState("saving");
    startPromiseToast();
  }, [startPromiseToast]);

  const markSaved = useCallback(() => {
    setSaveState("saved");
    if (pendingToast.current) {
      pendingToast.current.resolve();
      pendingToast.current = null;
    }
    clearTimer.current = setTimeout(() => setSaveState("idle"), 2000);
  }, []);

  const markError = useCallback((reason?: unknown) => {
    setSaveState("error");
    if (pendingToast.current) {
      pendingToast.current.reject(reason ?? new Error("Save failed"));
      pendingToast.current = null;
    } else {
      toast.error(toErrorMessage(reason));
    }
    clearTimer.current = setTimeout(() => setSaveState("idle"), 4000);
  }, []);

  useEffect(() => {
    return () => {
      if (clearTimer.current) {
        clearTimeout(clearTimer.current);
      }
      if (pendingToast.current) {
        if (
          typeof pendingToast.current.toastRef === "string" ||
          typeof pendingToast.current.toastRef === "number"
        ) {
          toast.dismiss(pendingToast.current.toastRef);
        }
        pendingToast.current = null;
      }
    };
  }, []);

  return { saveState, markSaving, markSaved, markError };
}
