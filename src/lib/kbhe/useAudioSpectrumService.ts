import { useEffect } from "react";
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

  const effectQ = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled: connected,
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const isAudioSpectrum = effectQ.data === LEDEffect.AUDIO_SPECTRUM;

  useEffect(() => {
    if (!connected || !isAudioSpectrum) return;

    let disposed = false;

    const tick = async () => {
      try {
        const bands = await invoke<number[]>("kbhe_get_audio_bands");
        if (!disposed) {
          await kbheDevice.setAudioSpectrum(bands, 200);
        }
      } catch {
        // Ignore errors (device disconnect, audio unavailable) — loop continues
      }
      if (!disposed) {
        void tick();
      }
    };

    void tick();

    return () => {
      disposed = true;
    };
  }, [connected, isAudioSpectrum]);
}
