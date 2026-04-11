import { cn } from "@/lib/utils";
import { IconCheck, IconLoader2, IconAlertTriangle } from "@tabler/icons-react";

export type SaveState = "idle" | "saving" | "saved" | "error";

interface Props {
  state: SaveState;
  errorMessage?: string;
  className?: string;
}

export function AutosaveStatus({ state, errorMessage, className }: Props) {
  if (state === "idle") return null;

  return (
    <span
      className={cn(
        "inline-flex items-center gap-1 text-xs font-medium transition-opacity",
        state === "saving" && "text-muted-foreground",
        state === "saved" && "text-green-600 dark:text-green-400",
        state === "error" && "text-destructive",
        className,
      )}
    >
      {state === "saving" && <IconLoader2 className="size-3 animate-spin" />}
      {state === "saved" && <IconCheck className="size-3" />}
      {state === "error" && <IconAlertTriangle className="size-3" />}
      {state === "saving" && "Saving…"}
      {state === "saved" && "Saved"}
      {state === "error" && (errorMessage ?? "Save failed")}
    </span>
  );
}

/** Hook to manage autosave state with auto-clear */
import { useState, useRef, useCallback } from "react";

export function useAutosave() {
  const [saveState, setSaveState] = useState<SaveState>("idle");
  const clearTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const markSaving = useCallback(() => {
    if (clearTimer.current) clearTimeout(clearTimer.current);
    setSaveState("saving");
  }, []);

  const markSaved = useCallback(() => {
    setSaveState("saved");
    clearTimer.current = setTimeout(() => setSaveState("idle"), 2000);
  }, []);

  const markError = useCallback(() => {
    setSaveState("error");
    clearTimer.current = setTimeout(() => setSaveState("idle"), 4000);
  }, []);

  return { saveState, markSaving, markSaved, markError };
}
