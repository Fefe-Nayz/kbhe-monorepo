import keyboardLayoutRaw from "@/assets/layouts/keyboard-layout.json?raw";
import keyboardLayoutFnRaw from "@/assets/layouts/keyboard-layout-fn.json?raw";

export type KeyMode = "single" | "multi";
export type displayedInfo = "regular" | "actuationMode" | "analogValues";
export type labelItems = string;
export type RotaryTargetId = "rotary.press" | "rotary.ccw" | "rotary.cw";

export interface KeyConfig {
  id: string;
  label: labelItems[];
  value: string;
  color?: string;
  fontSize?: number;
}

export interface KeyboardLayout {
  bindings: Record<string, KeyConfig>;
  rotaryBindings: Record<RotaryTargetId, KeyConfig>;
}

interface LegacyKeyboardLayout {
  keys?: Array<Array<{ id?: string; label?: string[]; value?: string }>>;
}

export interface PreviewKeyMeta {
  id: string;
  keyIndex: number;
  baseLabel: string;
  fnLabel: string;
}

function extractPrimaryLabel(value: string): string {
  const lines = value
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean);
  return lines[0] ?? "";
}

function flattenKleLabels(raw: string): string[] {
  const rows = JSON.parse(raw) as unknown[];
  const labels: string[] = [];

  for (const row of rows) {
    if (!Array.isArray(row)) {
      continue;
    }
    for (const item of row) {
      if (typeof item === "string") {
        labels.push(extractPrimaryLabel(item));
      }
    }
  }

  return labels;
}

const baseLabels = flattenKleLabels(keyboardLayoutRaw);
const fnLabels = flattenKleLabels(keyboardLayoutFnRaw);

export const keyboardPreviewBaseLayout = keyboardLayoutRaw;
export const keyboardPreviewFnLayout = keyboardLayoutFnRaw;

export const previewKeys: PreviewKeyMeta[] = baseLabels.map((baseLabel, keyIndex) => ({
  id: `key-${keyIndex}`,
  keyIndex,
  baseLabel,
  fnLabel: fnLabels[keyIndex] ?? "",
}));

export const previewKeyIds = previewKeys.map((key) => key.id);
export const LAYER_NAMES: Record<number, string> = {
  0: "Base",
  1: "Fn",
  2: "Layer 2",
  3: "Layer 3",
};
export const previewKeyMetaById = Object.fromEntries(
  previewKeys.map((key) => [key.id, key]),
) as Record<string, PreviewKeyMeta>;

export const rotaryTargetDefaults: Record<RotaryTargetId, KeyConfig> = {
  "rotary.press": { id: "rotary.press", label: ["Rotary Press"], value: "rotary.press" },
  "rotary.ccw": { id: "rotary.ccw", label: ["Rotary CCW"], value: "rotary.ccw" },
  "rotary.cw": { id: "rotary.cw", label: ["Rotary CW"], value: "rotary.cw" },
};

function buildDefaultBindings(): Record<string, KeyConfig> {
  return Object.fromEntries(
    previewKeys.map((key) => [
      key.id,
      {
        id: key.id,
        label: [key.baseLabel],
        value: key.baseLabel,
      },
    ]),
  );
}

export const defaultLayout: KeyboardLayout = {
  bindings: buildDefaultBindings(),
  rotaryBindings: rotaryTargetDefaults,
};

export function cloneDefaultLayout(): KeyboardLayout {
  return {
    bindings: Object.fromEntries(
      Object.entries(defaultLayout.bindings).map(([id, config]) => [id, { ...config, label: [...config.label] }]),
    ),
    rotaryBindings: {
      "rotary.press": { ...defaultLayout.rotaryBindings["rotary.press"], label: [...defaultLayout.rotaryBindings["rotary.press"].label] },
      "rotary.ccw": { ...defaultLayout.rotaryBindings["rotary.ccw"], label: [...defaultLayout.rotaryBindings["rotary.ccw"].label] },
      "rotary.cw": { ...defaultLayout.rotaryBindings["rotary.cw"], label: [...defaultLayout.rotaryBindings["rotary.cw"].label] },
    },
  };
}

function isModernLayout(value: unknown): value is KeyboardLayout {
  return Boolean(
    value &&
      typeof value === "object" &&
      "bindings" in value &&
      "rotaryBindings" in value,
  );
}

export function normalizeKeyboardLayout(value: unknown): KeyboardLayout {
  const normalized = cloneDefaultLayout();

  if (isModernLayout(value)) {
    Object.entries(value.bindings ?? {}).forEach(([id, config]) => {
      if (id in normalized.bindings) {
        normalized.bindings[id] = {
          ...normalized.bindings[id],
          ...config,
          label: Array.isArray(config.label) ? [...config.label] : normalized.bindings[id].label,
        };
      }
    });

    (Object.keys(normalized.rotaryBindings) as RotaryTargetId[]).forEach((id) => {
      const config = value.rotaryBindings?.[id];
      if (config) {
        normalized.rotaryBindings[id] = {
          ...normalized.rotaryBindings[id],
          ...config,
          label: Array.isArray(config.label)
            ? [...config.label]
            : normalized.rotaryBindings[id].label,
        };
      }
    });

    return normalized;
  }

  const legacy = value as LegacyKeyboardLayout;
  if (Array.isArray(legacy?.keys)) {
    legacy.keys.flat().forEach((key) => {
      if (!key?.id) {
        return;
      }
      const targetId = key.id === "encoder" ? "rotary.press" : key.id;
      if (targetId in normalized.bindings) {
        normalized.bindings[targetId] = {
          ...normalized.bindings[targetId],
          label: Array.isArray(key.label) ? [...key.label] : normalized.bindings[targetId].label,
          value: key.value ?? normalized.bindings[targetId].value,
        };
      } else if (targetId in normalized.rotaryBindings) {
        const rotaryId = targetId as RotaryTargetId;
        normalized.rotaryBindings[rotaryId] = {
          ...normalized.rotaryBindings[rotaryId],
          label: Array.isArray(key.label)
            ? [...key.label]
            : normalized.rotaryBindings[rotaryId].label,
          value: key.value ?? normalized.rotaryBindings[rotaryId].value,
        };
      }
    });
  }

  return normalized;
}
