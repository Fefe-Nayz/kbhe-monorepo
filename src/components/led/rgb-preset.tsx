import { useState } from "react"
import { Button } from "@/components/ui/button"
import { ScrollArea } from "@/components/ui/scroll-area"
import { presets } from "@/constants/rgbPresets"

export default function PresetSelector({ onSelect }: { onSelect?: (id: string) => void }) {
  const [selected, setSelected] = useState<string | null>(null)

  const handleSelect = (id: string) => {
    setSelected(id)
    onSelect?.(id)
  }

  return (
    <ScrollArea className="h-full w-full">
      <div className="space-y-4 p-2">
        {["Static", "Effect", "Reactive"].map((category) => (
          <div key={category}>
            <h3 className="font-semibold mb-2">{category}</h3>
            <div className="grid grid-cols-2 gap-2">
              {presets
                .filter((p) => p.type === category)
                .map((preset) => (
                  <Button
                    key={preset.id}
                    variant={selected === preset.id ? "default" : "outline"}
                    size="sm"
                    onClick={() => handleSelect(preset.id)}
                  >
                    {preset.name}
                  </Button>
                ))}
            </div>
          </div>
        ))}
      </div>
    </ScrollArea>
  )
}