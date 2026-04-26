import { useEffect } from "react";
import { useQuery } from "@tanstack/react-query";
import { useDeviceSession } from "@/lib/kbhe/session";
import { kbheDevice } from "@/lib/kbhe/device";
import { KEY_COUNT, LEDEffect } from "@/lib/kbhe/protocol";
import { queryKeys } from "@/lib/query/keys";
import { HID_TO_DOM_CODE, getOsKeyVariantsFromSystem, type OsKeyVariantEntry } from "@/hooks/use-os-layout";

function isSingleLetterLabel(value: string | undefined): boolean {
  if (!value) {
    return false;
  }

  const trimmed = value.trim();
  if (!trimmed) {
    return false;
  }

  return Array.from(trimmed).length === 1 && /^[A-Za-z]$/.test(trimmed);
}

function hasAlphaKeycapLabel(entry: OsKeyVariantEntry | undefined): boolean {
  if (!entry) {
    return false;
  }

  return (
    isSingleLetterLabel(entry.base) ||
    isSingleLetterLabel(entry.shift) ||
    isSingleLetterLabel(entry.altGr) ||
    isSingleLetterLabel(entry.shiftAltGr)
  );
}

/**
 * Global background service: sends a per-key alpha bitmask to the keyboard
 * whenever the ALPHA_MODS effect is active, so the firmware can correctly
 * distinguish alpha keys from modifier/punctuation keys regardless of the
 * OS keyboard layout language.
 */
export function useAlphaMaskService() {
  const connected = useDeviceSession((s) => s.status === "connected");

  const effectQ = useQuery({
    queryKey: queryKeys.led.effect(),
    queryFn: () => kbheDevice.getLedEffect(),
    enabled: connected,
    refetchInterval: 2000,
    staleTime: 1000,
  });

  const isAlphaModsActive = effectQ.data === LEDEffect.ALPHA_MODS;

  useEffect(() => {
    if (!connected || !isAlphaModsActive) return;

    let disposed = false;

    const fetchBaseLayerKeycodes = async (): Promise<number[] | null> => {
      const keycodes: number[] = Array.from({ length: KEY_COUNT }, () => 0);
      const BATCH_SIZE = 10;

      for (let start = 0; start < KEY_COUNT; start += BATCH_SIZE) {
        const end = Math.min(KEY_COUNT, start + BATCH_SIZE);
        const indexes = Array.from({ length: end - start }, (_, idx) => start + idx);
        const rows = await Promise.all(
          indexes.map(async (keyIndex) => {
            try {
              return await kbheDevice.getLayerKeycode(0, keyIndex);
            } catch {
              return null;
            }
          }),
        );

        for (let i = 0; i < rows.length; i += 1) {
          const keyIndex = indexes[i];
          const row = rows[i];
          if (!row) {
            return null;
          }
          keycodes[keyIndex] = row.hid_keycode;
        }
      }

      return keycodes;
    };

    const sendMask = async () => {
      try {
        const [keycodes, osVariants] = await Promise.all([
          fetchBaseLayerKeycodes(),
          getOsKeyVariantsFromSystem(),
        ]);

        if (disposed || !keycodes) return;

        const maskBytes = new Array(Math.ceil(keycodes.length / 8)).fill(0);

        for (let keyIndex = 0; keyIndex < keycodes.length; keyIndex += 1) {
          const hidKeycode = keycodes[keyIndex] ?? 0;
          const domCode = HID_TO_DOM_CODE[hidKeycode];
          let isAlpha = false;

          if (domCode && osVariants) {
            const variants = osVariants[domCode];
            if (variants) {
              isAlpha = hasAlphaKeycapLabel(variants);
            } else {
              // Missing OS entry for this code: fallback to HID A-Z range.
              isAlpha = hidKeycode >= 0x04 && hidKeycode <= 0x1d;
            }
          } else {
            // Fallback: QWERTY A-Z HID range
            isAlpha = hidKeycode >= 0x04 && hidKeycode <= 0x1d;
          }

          if (isAlpha) {
            const byteIdx = Math.floor(keyIndex / 8);
            const bitIdx = keyIndex % 8;
            if (byteIdx < maskBytes.length) {
              maskBytes[byteIdx] |= 1 << bitIdx;
            }
          }
        }

        if (!disposed) {
          await kbheDevice.setAlphaMask(maskBytes);
        }
      } catch {
        // Ignore errors
      }
    };

    void sendMask();

    // Resend on window focus (user may have switched OS layout)
    const onFocus = () => { void sendMask(); };
    window.addEventListener("focus", onFocus);

    return () => {
      disposed = true;
      window.removeEventListener("focus", onFocus);
    };
  }, [connected, isAlphaModsActive]);
}
