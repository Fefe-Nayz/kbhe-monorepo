import { clsx, type ClassValue } from "clsx"
import { twMerge } from "tailwind-merge"

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}

/**
 * Base UI Slider's onValueChange passes `number | readonly number[]`.
 * This helper safely extracts the single numeric value.
 */
export function sliderVal(v: number | readonly number[]): number | undefined {
  if (typeof v === "number") return v;
  return (v as readonly number[])[0];
}
