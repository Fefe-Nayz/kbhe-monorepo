import { useEffect, useRef } from "react";
import { useQuery } from "@tanstack/react-query";
import { invoke } from "@tauri-apps/api/core";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { LEDEffect } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";

/**
 * Global background service: streams PC audio FFT bands to the keyboard
 * whenever the AUDIO_SPECTRUM effect is active, regardless of which page
 * is currently open.
 */
export function useAudioSpectrumService() {
  const connected = useDeviceSession((s) => s.status === "connected");
  const fastBassEnvRef = useRef(0);
  const slowBassEnvRef = useRef(0);
  const impactEnvRef = useRef(0);

  const effectQ = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled: connected,
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const isAudioActive =
    effectQ.data === LEDEffect.AUDIO_SPECTRUM ||
    effectQ.data === LEDEffect.IMPACT_RAINBOW ||
    effectQ.data === LEDEffect.BASS_RIPPLE;

  useEffect(() => {
    if (!connected || !isAudioActive) return;

    let disposed = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const TARGET_LOOP_MS = 18;

    const tick = async () => {
      const startedAt = performance.now();
      try {
        const bands = await invoke<number[]>("kbhe_get_audio_bands");
        if (!disposed && Array.isArray(bands) && bands.length > 0) {
          const b0 = bands[0] ?? 0;
          const b1 = bands[1] ?? 0;
          const b2 = bands[2] ?? 0;
          const b3 = bands[3] ?? 0;
          const b4 = bands[4] ?? 0;
          const noiseBands = [bands[8] ?? 0, bands[9] ?? 0, bands[10] ?? 0, bands[11] ?? 0, bands[12] ?? 0, bands[13] ?? 0, bands[14] ?? 0, bands[15] ?? 0];
          const noiseFloor = Math.round(noiseBands.reduce((sum, value) => sum + value, 0) / noiseBands.length);

          const bass = Math.min(255, Math.round((b0 * 5 + b1 * 4 + b2 * 3 + b3 * 2 + b4) / 15));

          if (bass > fastBassEnvRef.current) {
            const rise = Math.max(3, Math.round((bass - fastBassEnvRef.current) * 0.42));
            fastBassEnvRef.current = Math.min(bass, fastBassEnvRef.current + rise);
          } else {
            fastBassEnvRef.current = Math.max(0, fastBassEnvRef.current - 6);
          }

          if (bass > slowBassEnvRef.current) {
            const rise = Math.max(1, Math.round((bass - slowBassEnvRef.current) * 0.08));
            slowBassEnvRef.current = Math.min(bass, slowBassEnvRef.current + rise);
          } else {
            slowBassEnvRef.current = Math.max(0, slowBassEnvRef.current - 1);
          }

          const transient = Math.max(0, fastBassEnvRef.current - slowBassEnvRef.current);
          const gate = Math.max(6, Math.round(noiseFloor * 0.45) + 4);
          const bassOverGate = Math.max(0, bass - gate);
          const bassNormDenom = Math.max(1, 255 - gate);
          const bassAnalog = Math.min(255, Math.round((bassOverGate * 255) / bassNormDenom));
          const transientAnalog = Math.min(255, transient * 5);

          // Continuous analog impact: low-level noise is suppressed, strong bass stays expressive.
          const impactLinear = Math.round((bassAnalog * 4 + transientAnalog * 3) / 7);
          const impactShaped = Math.min(255, Math.round((impactLinear * impactLinear) / 255));
          const impactTarget = Math.min(255, Math.round((impactLinear * 3 + impactShaped) / 4));

          if (impactTarget > impactEnvRef.current) {
            const rise = Math.max(3, Math.round((impactTarget - impactEnvRef.current) * 0.5));
            impactEnvRef.current = Math.min(impactTarget, impactEnvRef.current + rise);
          } else {
            impactEnvRef.current = Math.max(0, impactEnvRef.current - 7);
          }

          const impactLevel = impactEnvRef.current <= 2 ? 0 : impactEnvRef.current;

          await kbheDevice.setAudioSpectrum(bands, impactLevel);
        }
      } catch {
        // Ignore errors (device disconnect, audio unavailable) — loop continues
      }
      if (!disposed) {
        const elapsed = performance.now() - startedAt;
        const waitMs = Math.max(0, TARGET_LOOP_MS - elapsed);
        timer = setTimeout(() => {
          void tick();
        }, waitMs);
      }
    };

    void tick();

    return () => {
      disposed = true;
      if (timer) {
        clearTimeout(timer);
      }
      fastBassEnvRef.current = 0;
      slowBassEnvRef.current = 0;
      impactEnvRef.current = 0;
    };
  }, [connected, isAudioActive]);
}
