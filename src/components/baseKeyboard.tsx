import { useEffect } from "react";

import {
  keyboardPreviewBaseLayout,
  LAYER_NAMES,
  previewKeyMetaById,
  type RotaryTargetId,
} from "@/constants/defaultLayout";
import { KeyboardLayout } from "@/lib/vendor/react-kle-modern";
import { useKeyboardStore } from "@/stores/keyboard-store";
import { labelRegistry } from "@/ui/labels/labelRegistry";

interface BaseKeyboardProps {
  mode: "single" | "multi";
  onButtonClick: (ids: string[] | string) => void;
}

function resolveLabel(label: string): React.ReactNode {
  return labelRegistry[label] ?? label;
}

function RotaryPreview({
  selectedKeys,
  onSelect,
  labels,
}: {
  selectedKeys: string[];
  onSelect: (id: RotaryTargetId) => void;
  labels: Record<RotaryTargetId, string>;
}) {
  return (
    <div className="flex flex-col items-center justify-center gap-3 rounded-2xl border border-slate-300 bg-slate-50 px-5 py-4 shadow-sm">
      <span className="text-[11px] font-semibold uppercase tracking-[0.2em] text-slate-500">
        Rotary
      </span>
      <div className="flex items-center gap-3">
        <button
          type="button"
          onClick={() => onSelect("rotary.ccw")}
          className={`rounded-full border px-3 py-2 text-xs font-medium transition ${
            selectedKeys.includes("rotary.ccw")
              ? "border-blue-500 bg-blue-500 text-white"
              : "border-slate-300 bg-white text-slate-700 hover:border-blue-300"
          }`}
        >
          {labels["rotary.ccw"]}
        </button>
        <button
          type="button"
          onClick={() => onSelect("rotary.press")}
          className={`flex h-20 w-20 items-center justify-center rounded-full border text-center text-xs font-semibold transition ${
            selectedKeys.includes("rotary.press")
              ? "border-blue-500 bg-blue-500 text-white shadow-[0_0_0_6px_rgba(59,130,246,0.18)]"
              : "border-slate-300 bg-white text-slate-700 hover:border-blue-300"
          }`}
        >
          {labels["rotary.press"]}
        </button>
        <button
          type="button"
          onClick={() => onSelect("rotary.cw")}
          className={`rounded-full border px-3 py-2 text-xs font-medium transition ${
            selectedKeys.includes("rotary.cw")
              ? "border-blue-500 bg-blue-500 text-white"
              : "border-slate-300 bg-white text-slate-700 hover:border-blue-300"
          }`}
        >
          {labels["rotary.cw"]}
        </button>
      </div>
    </div>
  );
}

export default function BaseKeyboard({ mode = "single", onButtonClick }: BaseKeyboardProps) {
  const selectedKeys = useKeyboardStore((state) => state.selectedKeys);
  const toggleKeySelection = useKeyboardStore((state) => state.toggleKeySelection);
  const setMode = useKeyboardStore((state) => state.setMode);
  const setCurrentLayer = useKeyboardStore((state) => state.setCurrentLayer);
  const layout = useKeyboardStore((state) => state.layout);
  const currentLayer = useKeyboardStore((state) => state.currentLayer);

  const handleSelection = (id: string) => {
    const alreadySelected = selectedKeys.includes(id);
    const nextSelection =
      mode === "single"
        ? alreadySelected
          ? []
          : [id]
        : alreadySelected
          ? selectedKeys.filter((key) => key !== id)
          : [...selectedKeys, id];

    toggleKeySelection(id);
    onButtonClick(mode === "single" ? (nextSelection[0] ?? "") : nextSelection);
  };

  useEffect(() => {
    setMode(mode);
  }, [mode, setMode]);

  const rotaryLabels: Record<RotaryTargetId, string> = {
    "rotary.ccw": layout.rotaryBindings["rotary.ccw"]?.label[0] ?? "Rotary CCW",
    "rotary.press": layout.rotaryBindings["rotary.press"]?.label[0] ?? "Rotary Press",
    "rotary.cw": layout.rotaryBindings["rotary.cw"]?.label[0] ?? "Rotary CW",
  };

  const keyboardSelectedIds = selectedKeys.filter((key) => key.startsWith("key-"));

  return (
    <div className="w-full overflow-x-auto rounded-2xl border border-slate-200 bg-white px-5 py-5 shadow-sm">
      <div className="mb-4 flex items-center gap-2">
        {Object.entries(LAYER_NAMES).map(([layer, name]) => {
          const layerIndex = Number(layer);
          return (
            <button
              key={layer}
              type="button"
              onClick={() => setCurrentLayer(layerIndex)}
              className={`rounded-full px-3 py-1.5 text-xs font-semibold transition ${
                currentLayer === layerIndex
                  ? "bg-blue-500 text-white"
                  : "bg-slate-100 text-slate-700 hover:bg-slate-200"
              }`}
            >
              {name}
            </button>
          );
        })}
      </div>
      <div className="flex min-w-max items-start gap-8">
        <KeyboardLayout
          layout={keyboardPreviewBaseLayout}
          theme="modern"
          interactive
          unit={50}
          gap={5}
          selectedKeyIds={keyboardSelectedIds}
          onKeyClick={(key) => handleSelection(key.id)}
          renderLegend={({ key, index }) => {
            if (index !== 0) {
              return null;
            }
            const meta = previewKeyMetaById[key.id];
            const configuredLabel = layout.bindings[key.id]?.label[0];
            const layerDefault =
              currentLayer === 1 ? meta?.fnLabel || meta?.baseLabel || "" : meta?.baseLabel || "";
            return resolveLabel(configuredLabel || layerDefault);
          }}
        />

        <RotaryPreview
          selectedKeys={selectedKeys}
          onSelect={handleSelection}
          labels={rotaryLabels}
        />
      </div>
    </div>
  );
}
