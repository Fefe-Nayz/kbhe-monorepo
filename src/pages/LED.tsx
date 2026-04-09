import ColorPickerCard from "@/components/color-picker"
import BaseKeyboard from "@/components/baseKeyboard"

export default function LED() {

  return (
    <div className="p-8">
      <div className="mt-6 hidden">
                <BaseKeyboard mode="multi" 
                onButtonClick={(ids) => console.log("All pressed keys:", ids)} />
                </div>
     <div className="w-full inline-flex overflow-auto rounded-3xl border border-border bg-background p-4">
      <ColorPickerCard />
    </div>

    </div>

  )
}
