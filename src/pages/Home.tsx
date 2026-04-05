import BaseKeyboard from "@/components/baseKeyboard"
import KeyMapper from "@/components/keyMapper"
import { useKeyboardStore } from "@/stores/keyboard-store"
import { useEffect } from "react"

export default function Home() {
  const selectedKeys = useKeyboardStore(state => state.selectedKeys)
  const updateKeyConfig = useKeyboardStore(state => state.updateKeyConfig)
  useEffect(() => {
    useKeyboardStore.getState().setSaveEnabled(true)

    return () => {
      useKeyboardStore.getState().setSaveEnabled(false)
    }
  }, [])

  return (
    <div className="bg-green-700 justify-center items-center p-4 h-[88vh]">
      {/*<h1 className="text-2xl font-bold mb-4">Welcome to the KBHE Configurator</h1>*/}
      <div className="bg-blue-600 flex justify-center mb-1">
        <BaseKeyboard
          mode="multi"
          onButtonClick={(ids) => console.log("All pressed keys:", ids)}
        />
      </div>
      <div className="bg-red-600 flex  overflow-auto">
        {/*<KeyMapper onButtonClick={(id) => console.log("You clicked on ", id)} />*/}
        <KeyMapper onButtonClick={(key) => {
          if (selectedKeys.length === 0) return
          updateKeyConfig(selectedKeys, {
            label: [key.label],
            value: key.value,
          })
          //empty the selected keys after updating
          useKeyboardStore.getState().clearSelection()
        }} />
      </div>

    </div>
  )
}