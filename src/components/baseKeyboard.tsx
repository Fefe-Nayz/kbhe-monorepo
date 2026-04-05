import { useKeyboardStore } from "@/stores/keyboard-store"
import { labelRegistry } from "@/ui/labels/labelRegistry"

import Key from "./keyboard-components/key"
import KeyEnter from "./keyboard-components/keyEnter"

import { useScreenScale } from "@/hooks/mywindow"
import { useEffect } from "react"



interface BaseKeyboardProps {
  mode: "single" | "multi";
  onButtonClick: (ids: string[] | string) => void;

}

const resolveLabel = (label: string): React.ReactNode => {
  return labelRegistry[label] ?? label
}


export default function BaseKeyboard({ mode = "single", onButtonClick }: BaseKeyboardProps) {
  const scale = useScreenScale();

  const selectedKeys = useKeyboardStore(state => state.selectedKeys)
  const toggleKeySelection = useKeyboardStore(state => state.toggleKeySelection)
  const setMode = useKeyboardStore(state => state.setMode) 
  const layout = useKeyboardStore(state => state.layout)

  const handleKeyClick = (id: string) => {
    toggleKeySelection(id)

    if (mode === "single") {
      onButtonClick(id)
    } else {
      // Recalcule la nouvelle sélection après toggle
      const updated = selectedKeys.includes(id)
        ? selectedKeys.filter(k => k !== id)
        : [...selectedKeys, id]
      onButtonClick(updated)
    }
  }

  //const keyboard75Layout = defaultLayout.keys;
  const keyboard75Layout = layout.keys;  

  useEffect(() => {
    console.log("Current scale:", scale);
  }, [scale]);

  useEffect(() => {
    setMode(mode)
  }, [mode])
  return (
    <div className="flex flex-col gap-2 p-4 bg-white rounded-lg border border-gray-200 w-fit h-auto">
      {keyboard75Layout.map((row, rowIndex) => (
        <div key={rowIndex} className="flex gap-1">
          {row.map((keyData) => (
            <div key={keyData.id} style={{ marginLeft: `${keyData.gap * 0.25}rem` }}>
              {keyData.id === "enter" ? (
                <KeyEnter
                  id={keyData.id}
                  label={resolveLabel(keyData.label[0] as string)}
                  width={keyData.width * scale}
                  value={keyData.value}
                  onSelect={handleKeyClick}
                />
              ) : (
                <Key
                  id={keyData.id}
                  label={resolveLabel(keyData.label[0] as string)}
                  width={keyData.width * scale}
                  value={keyData.value}
                  onSelect={handleKeyClick}
                  className={`${
                    selectedKeys.includes(keyData.id)
                      ? "bg-red-700 active:bg-red-500"
                      : "bg-transparent active:bg-blue-300"
                  }`}
                />
              )}
            </div>
          ))}
        </div>
      ))}
    </div>
  )
}