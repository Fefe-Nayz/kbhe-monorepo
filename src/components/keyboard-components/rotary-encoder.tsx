import { useKeyboardStore } from "@/stores/keyboard-store"

export default function RotaryEncoder() {
  const selectedKeys = useKeyboardStore(state => state.selectedKeys)
  const toggleKeySelection = useKeyboardStore(state => state.toggleKeySelection)

  const isSelected = selectedKeys.includes("encoder")

  return (
    <div
      onClick={() => toggleKeySelection("encoder")}
      className={`
        w-10 h-10 rounded-full border-2 cursor-pointer
        flex items-center justify-center text-xs font-medium
        transition-colors
        ${isSelected
          ? "bg-red-500 border-red-400 text-white"
          : "bg-gray-100 border-white-300 text-gray-600 hover:bg-gray-200"
        }
      `}
    >
        
    </div>
  )
}