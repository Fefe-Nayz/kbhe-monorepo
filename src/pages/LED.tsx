import ColorPickerCard from "@/components/color-picker"
import BaseKeyboard from "@/components/baseKeyboard"
import PresetSelector from "@/components/led/rgb-preset"

export default function LED() {

  return (
    <main>
      <div className="mt-6 hidden">
        <BaseKeyboard mode="multi"
          onButtonClick={(ids) => console.log("All pressed keys:", ids)} />
      </div>
      <div className="flex h-full gap-4 bg-red-500">


        <div className="p-4 h-75 border flex flex-col overflow-hidden">
          <PresetSelector onSelect={(id) => console.log("Preset choisi :", id)} />
        </div>


        <div className="w-76 flex flex-col rounded-3xl border border-border bg-background p-1">
          <ColorPickerCard />
        </div>

        <div className="bg-green-700">
          <h1>Informations</h1>
        </div>

      </div>
    </main>


  )
}
