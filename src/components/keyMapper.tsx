import { Input } from "./ui/input"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Button } from "@/components/ui/button"
import Key from "./keyboard-components/key"
import KnobMapper from "./knobMapper"
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion"
import { useState } from "react"
import { AllKeys } from "@/constants/allKeys"

interface KeyMapperProps {
  onButtonClick: (key: { id: string, label: string, value: string, width: number }) => void
}

export default function KeyMapper({ onButtonClick }: KeyMapperProps) {
  const [mapperMode, setMapperMode] = useState<"keys" | "knob">("keys")
  const [searchTerm, setSearchTerm] = useState("")

  const handleKeyClick = (id: string) => {
    const key = AllKeys.find(k => k.id === id)
    if (!key) return
    onButtonClick({ id: key.id, label: key.label, value: key.value, width: key.width })
  }

  const groupedKeys = AllKeys.reduce((acc, key) => {
    if (!acc[key.type]) acc[key.type] = []
    acc[key.type].push(key)
    return acc
  }, {} as Record<string, typeof AllKeys>)

  const funcFilter = (_type: string, searchTerm: string, keys: typeof AllKeys) => {
    if (searchTerm === "") return true
    return keys.some(key => key.id.toLowerCase().includes(searchTerm.toLowerCase()))
  }

  return (
    <div className="flex flex-col gap-2 p-4 bg-slate-50 border border-gray-200 w-full rounded-md">

      {/* Toggle Keys / Knob */}
      <div className="flex w-full gap-2">
        <Button
          variant={mapperMode === "keys" ? "default" : "outline"}
          size="sm"
          className="flex-1"
          onClick={() => setMapperMode("keys")}
        >
          Keys
        </Button>
        <Button
          variant={mapperMode === "knob" ? "default" : "outline"}
          size="sm"
          className="flex-1"
          onClick={() => setMapperMode("knob")}
        >
          Knob
        </Button>
      </div>

      {/* Mode Keys */}
      {mapperMode === "keys" && (
        <div className="flex flex-col gap-2">
          <Input
            placeholder="Search for a component here..."
            value={searchTerm}
            onChange={(e) => setSearchTerm(e.target.value)}
          />
          <ScrollArea className="h-48 w-full rounded-md border p-2">
            {Object.entries(groupedKeys)
              .filter(([type, keys]) => funcFilter(type, searchTerm, keys))
              .map(([type, keys]) => (
                <Accordion key={type} defaultValue={["Basic"]} className="w-full">
                  <AccordionItem value={type} disabled={searchTerm !== ""}>
                    <AccordionTrigger className="bg-slate-50 transition-colors px-4">
                      <span className="text-sm font-bold tracking-wide text-slate-800 uppercase">
                        {type}
                      </span>
                    </AccordionTrigger>
                    <AccordionContent>
                      <div className="flex flex-wrap gap-1 p-1">
                        {keys
                          .filter(k => k.id.toLowerCase().includes(searchTerm.toLowerCase()))
                          .map(keyData => (
                            <Key
                              key={keyData.id}
                              id={keyData.id}
                              label={keyData.label}
                              width={keyData.width}
                              value={keyData.value}
                              onSelect={handleKeyClick}
                            />
                          ))}
                      </div>
                    </AccordionContent>
                  </AccordionItem>
                </Accordion>
              ))}
          </ScrollArea>
        </div>
      )}

      {/* Mode Knob */}
      {mapperMode === "knob" && (
        <KnobMapper
          onPressSelect={(key) => console.log("Knob press →", key)}
          onRotateLeft={(action) => console.log("Rotate left →", action)}
          onRotateRight={(action) => console.log("Rotate right →", action)}
        />
      )}

    </div>
  )
}