import { useEffect } from "react"
import BaseKeyboard from "@/components/baseKeyboard"
import PerformanceButtonDiv from "@/components/div/divPerformance"
import PerformanceZone from "@/components/performance-zone"
import { useKeyboardStore } from "@/stores/keyboard-store"

export default function Performance() {

  useEffect(() => {
    useKeyboardStore.getState().setSaveEnabled(true)
    return () => {
      useKeyboardStore.getState().setSaveEnabled(false)
    }
  }, [])

  return (
    <main className="h-screen flex flex-col overflow-hidden">

      {/* Clavier — fixe en haut */}
      <div className="flex-shrink-0 flex justify-center items-center border-b border-gray-200 px-8 py-4 overflow-x-auto">
        <BaseKeyboard
          mode="multi"
          onButtonClick={(ids) => console.log("Selected keys:", ids)}
        />
      </div>

      {/* Boutons d'action */}
      <div className="flex-shrink-0">
        <PerformanceButtonDiv />
      </div>

      {/* Zone scrollable */}
      <div className="flex-1 overflow-y-auto flex flex-col items-center py-4 gap-4">
        <PerformanceZone />
      </div>

    </main>
  )
}