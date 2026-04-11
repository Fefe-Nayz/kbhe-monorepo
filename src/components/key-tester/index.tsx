import { useState, useEffect, useRef, useCallback } from "react";
import { Badge } from "@/components/ui/badge";
import { ScrollArea } from "@/components/ui/scroll-area";
import { kbheDevice } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { HID_KEYCODE_NAMES } from "@/lib/kbhe/protocol";
import { cn } from "@/lib/utils";

interface KeyTesterProps {
  className?: string;
  pressHeight?: string;
  releaseHeight?: string;
}

export function KeyTester({
  className,
  pressHeight = "h-24",
  releaseHeight = "h-24",
}: KeyTesterProps) {
  const { status } = useDeviceSession();
  const connected = status === "connected";

  const [pressedKeys, setPressedKeys] = useState<string[]>([]);
  const [releasedKeys, setReleasedKeys] = useState<string[]>([]);
  const prevPressed = useRef(new Set<number>());

  const poll = useCallback(async () => {
    if (!connected) return;
    try {
      const snapshot = await kbheDevice.getKeyStates();
      if (!snapshot) return;
      const nowPressed = new Set<number>();
      for (let i = 0; i < snapshot.states.length; i++) {
        if (snapshot.states[i]) nowPressed.add(i);
      }

      for (const idx of nowPressed) {
        if (!prevPressed.current.has(idx)) {
          const name = HID_KEYCODE_NAMES[idx] ?? `K${idx + 1}`;
          setPressedKeys((prev) => {
            if (prev.includes(name)) return prev;
            return [...prev, name];
          });
        }
      }

      for (const idx of prevPressed.current) {
        if (!nowPressed.has(idx)) {
          const name = HID_KEYCODE_NAMES[idx] ?? `K${idx + 1}`;
          setReleasedKeys((prev) => [name, ...prev].slice(0, 20));
          setPressedKeys((prev) => prev.filter((k) => k !== name));
        }
      }

      prevPressed.current = nowPressed;
    } catch {
      // ignore polling errors
    }
  }, [connected]);

  useEffect(() => {
    if (!connected) return;
    const id = setInterval(() => void poll(), 50);
    return () => clearInterval(id);
  }, [connected, poll]);

  return (
    <div className={cn("flex flex-col gap-4", className)}>
      <div className="flex flex-col gap-2">
        <span className="text-sm font-medium">Pressed Keys</span>
        <div className={cn("overflow-hidden rounded-md border", pressHeight)}>
          <div className="flex flex-wrap gap-2 p-2">
            {pressedKeys.map((k) => (
              <Badge key={k} variant="default">{k}</Badge>
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
            {releasedKeys.map((k, i) => (
              <Badge key={`${k}-${i}`} variant="secondary">{k}</Badge>
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
