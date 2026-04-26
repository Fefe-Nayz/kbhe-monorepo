import { useEffect, useRef } from "react";
import { useQueryClient } from "@tanstack/react-query";
import { isTauri } from "@tauri-apps/api/core";
import { getCurrentWindow } from "@tauri-apps/api/window";
import {
  clearCloseLightingRestoreSnapshot,
  getCloseLightingPreferences,
  getCloseLightingRestoreSnapshot,
  setCloseLightingRestoreSnapshot,
} from "@/lib/app-startup";
import { kbheDevice } from "@/lib/kbhe/device";
import { useDeviceSession } from "@/lib/kbhe/session";
import { queryKeys } from "@/lib/query/keys";

/**
 * Applies a user-selected LED effect when the app window is closed,
 * then optionally restores the previous effect on the next app startup.
 */
export function useCloseLightingService() {
  const queryClient = useQueryClient();
  const connected = useDeviceSession((state) => state.status === "connected");
  const connectedRef = useRef(connected);
  const restoreAttemptedRef = useRef(false);
  const restoringRef = useRef(false);

  useEffect(() => {
    connectedRef.current = connected;
  }, [connected]);

  useEffect(() => {
    if (!connected || restoringRef.current || restoreAttemptedRef.current) {
      return;
    }

    const preferences = getCloseLightingPreferences();
    if (!preferences.restorePreviousOnStartup) {
      clearCloseLightingRestoreSnapshot();
      restoreAttemptedRef.current = true;
      return;
    }

    const snapshot = getCloseLightingRestoreSnapshot();
    if (!snapshot) {
      restoreAttemptedRef.current = true;
      return;
    }

    restoringRef.current = true;

    const restore = async () => {
      try {
        const restored = await kbheDevice.setLedEffect(snapshot.effect);
        if (restored) {
          clearCloseLightingRestoreSnapshot();
          await queryClient.invalidateQueries({ queryKey: queryKeys.led.effect() });
        }
      } catch {
        // Keep snapshot for a future startup retry.
      } finally {
        restoreAttemptedRef.current = true;
        restoringRef.current = false;
      }
    };

    void restore();
  }, [connected, queryClient]);

  useEffect(() => {
    if (!isTauri()) {
      return;
    }

    let unlisten: (() => void) | undefined;
    let applyingCloseEffect = false;

    const setup = async () => {
      try {
        const window = getCurrentWindow();
        unlisten = await window.onCloseRequested(async (event) => {
          const preferences = getCloseLightingPreferences();
          if (!preferences.enabled || applyingCloseEffect) {
            return;
          }

          event.preventDefault();
          applyingCloseEffect = true;

          try {
            if (connectedRef.current) {
              const currentEffect = await kbheDevice.getLedEffect();
              if (preferences.restorePreviousOnStartup && currentEffect != null) {
                setCloseLightingRestoreSnapshot(currentEffect);
              } else if (!preferences.restorePreviousOnStartup) {
                clearCloseLightingRestoreSnapshot();
              }

              await kbheDevice.setLedEffect(preferences.closeEffect);
              await queryClient.invalidateQueries({ queryKey: queryKeys.led.effect() });
            }
          } catch {
            // Close behavior remains best effort.
          } finally {
            applyingCloseEffect = false;
            try {
              await window.hide();
            } catch {
              // Ignore hide failures.
            }
          }
        });
      } catch {
        // Ignore setup errors and keep default close behavior.
      }
    };

    void setup();

    return () => {
      unlisten?.();
    };
  }, [queryClient]);
}
