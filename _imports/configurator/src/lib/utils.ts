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

/**
 * Build a base-ui Select `items` array from a `Record<label, value>` map.
 * base-ui Select.Root uses `items` to render human labels in the trigger.
 */
/**
 * Build a base-ui Select `items` array from a `Record<label, value>` map.
 * base-ui Select.Root uses `items` to render human labels in the trigger.
 */
export function selectItems(map: Record<string, string | number>): { value: string; label: string }[] {
  return Object.entries(map).map(([label, val]) => ({ value: String(val), label }));
}

/** Same as selectItems but for maps where keys are values and values are labels. */
export function selectItemsReverse(map: Record<string | number, string>): { value: string; label: string }[] {
  return Object.entries(map).map(([val, label]) => ({ value: String(val), label }));
}
