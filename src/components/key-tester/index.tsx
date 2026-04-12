import { useState, useEffect, useRef, useCallback } from "react";
import { Badge } from "@/components/ui/badge";
import { Switch } from "@/components/ui/switch";
import { ScrollArea } from "@/components/ui/scroll-area";
import { previewKeys } from "@/constants/defaultLayout";
import { kbheDevice, type KeyStatesSnapshot } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { cn } from "@/lib/utils";

function getKeyLabel(keyIndex: number): string {
  if (keyIndex < 0 || keyIndex >= previewKeys.length) {
    return `K${keyIndex + 1}`;
  }
  return previewKeys[keyIndex]?.baseLabel?.trim() || `K${keyIndex + 1}`;
}

interface KeyTesterProps {
  className?: string;
  pressHeight?: string;
  releaseHeight?: string;
  snapshot?: KeyStatesSnapshot | null;
  pollIntervalMs?: number;
  valueLabel?: string;
  labelFormatter?: (args: {
    keyIndex: number;
    snapshot: KeyStatesSnapshot | null;
    state: "pressed" | "released";
  }) => string;
}

export function KeyTester({
  className,
  pressHeight = "h-24",
  releaseHeight = "h-24",
  snapshot,
  pollIntervalMs = 80,
  valueLabel = "Distance",
  labelFormatter,
}: KeyTesterProps) {
  const { status } = useDeviceSession();
  const connected = status === "connected";
  const hasExternalSnapshot = snapshot !== undefined;
  const [pressedDisplayMode, setPressedDisplayMode] = useState<"label" | "value">(
    labelFormatter ? "value" : "label",
  );

  const [pressedKeys, setPressedKeys] = useState<number[]>([]);
  const [releasedKeys, setReleasedKeys] = useState<Array<{
    id: number;
    keyIndex: number;
    snapshot: KeyStatesSnapshot | null;
  }>>([]);
  const prevPressed = useRef(new Set<number>());
  const lastSnapshotRef = useRef<KeyStatesSnapshot | null>(null);
  const releaseIdRef = useRef(0);

  const formatKeyLabel = useCallback((
    keyIndex: number,
    state: "pressed" | "released",
    sourceSnapshot: KeyStatesSnapshot | null,
    mode: "label" | "value" = "value",
  ): string => {
    if (mode === "value" && labelFormatter) {
      const label = labelFormatter({ keyIndex, snapshot: sourceSnapshot, state });
      if (label) {
        return label;
      }
    }
    return getKeyLabel(keyIndex);
  }, [labelFormatter]);

  useEffect(() => {
    if (!labelFormatter && pressedDisplayMode !== "label") {
      setPressedDisplayMode("label");
    }
  }, [labelFormatter, pressedDisplayMode]);

  const applySnapshot = useCallback((next: KeyStatesSnapshot) => {
    lastSnapshotRef.current = next;
    const nowPressed = new Set<number>();
    for (let i = 0; i < next.states.length; i++) {
      if (next.states[i]) nowPressed.add(i);
    }

      for (const idx of nowPressed) {
        if (!prevPressed.current.has(idx)) {
          setPressedKeys((prev) => {
            if (prev.includes(idx)) return prev;
            return [...prev, idx];
          });
        }
      }

      for (const idx of prevPressed.current) {
        if (!nowPressed.has(idx)) {
          setReleasedKeys((prev) => [
            { id: releaseIdRef.current++, keyIndex: idx, snapshot: next },
            ...prev,
          ].slice(0, 20));
          setPressedKeys((prev) => prev.filter((k) => k !== idx));
        }
      }

      prevPressed.current = nowPressed;
      }, [formatKeyLabel]);

  useEffect(() => {
    if (!connected) {
      prevPressed.current = new Set<number>();
      lastSnapshotRef.current = null;
      releaseIdRef.current = 0;
      setPressedKeys([]);
      setReleasedKeys([]);
      return;
    }

    if (!hasExternalSnapshot || !snapshot) {
      return;
    }

    applySnapshot(snapshot);
  }, [applySnapshot, connected, hasExternalSnapshot, snapshot]);

  useEffect(() => {
    if (!connected || hasExternalSnapshot) {
      return;
    }

    let disposed = false;
    let timer: ReturnType<typeof setTimeout> | null = null;
    let inFlight = false;

    const tick = async () => {
      if (disposed || inFlight) {
        return;
      }

      inFlight = true;
      try {
        const next = await kbheDevice.getKeyStates();
        if (disposed || !next) {
          return;
        }
        applySnapshot(next);
      } catch {
        // ignore polling errors
      } finally {
        inFlight = false;
        if (!disposed) {
          timer = setTimeout(() => void tick(), pollIntervalMs);
        }
      }
    };

    void tick();

    return () => {
      disposed = true;
      if (timer) {
        clearTimeout(timer);
      }
    };
  }, [applySnapshot, connected, hasExternalSnapshot, pollIntervalMs]);

  return (
    <div className={cn("flex flex-col gap-4", className)}>
      <div className="flex flex-col gap-2">
        <div className="flex items-center justify-between gap-2">
          <span className="text-sm font-medium">Pressed Keys</span>
          {labelFormatter && (
            <div className="flex items-center gap-2">
              <span className="text-xs text-muted-foreground">Label</span>
              <Switch
                checked={pressedDisplayMode === "value"}
                onCheckedChange={(checked) => setPressedDisplayMode(checked ? "value" : "label")}
                aria-label="Toggle pressed keys display mode"
              />
              <span className="text-xs text-muted-foreground">{valueLabel}</span>
            </div>
          )}
        </div>
        <div className={cn("overflow-hidden rounded-md border", pressHeight)}>
          <div className="flex flex-wrap gap-2 p-2">
            {pressedKeys.map((k) => (
              <Badge key={k} variant="default">
                {formatKeyLabel(
                  k,
                  "pressed",
                  lastSnapshotRef.current,
                  pressedDisplayMode === "value" ? "value" : "label",
                )}
              </Badge>
            ))}
            {pressedKeys.length === 0 && (
              <span className="text-xs text-muted-foreground">Press a key...</span>
            )}
          </div>
        </div>
      </div>
      <div className="flex flex-col gap-2">
        <span className="text-sm font-medium">Released Keys</span>
        <ScrollArea className={cn("rounded-md border", releaseHeight)}>
          <div className="flex flex-wrap gap-2 p-2">
            {releasedKeys.map((entry) => (
              <Badge key={entry.id} variant="secondary">
                {formatKeyLabel(
                  entry.keyIndex,
                  "released",
                  entry.snapshot,
                  pressedDisplayMode === "value" ? "value" : "label",
                )}
              </Badge>
            ))}
            {releasedKeys.length === 0 && (
              <span className="text-xs text-muted-foreground">No releases yet</span>
            )}
          </div>
        </ScrollArea>
      </div>
    </div>
  );
}
