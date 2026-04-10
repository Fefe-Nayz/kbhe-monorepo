import { useState } from "react"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { Label } from "@/components/ui/label"
import { Input } from "@/components/ui/input"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Button } from "@/components/ui/button"
import Key from "./keyboard-components/key"
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion"
import { AllKeys } from "@/constants/allKeys"

const rotaryActions = [
  { value: "volume_up", label: "Volume Up" },
  { value: "volume_down", label: "Volume Down" },
  { value: "brightness_up", label: "Brightness Up" },
  { value: "brightness_down", label: "Brightness Down" },
  { value: "next_track", label: "Next Track" },
  { value: "prev_track", label: "Previous Track" },
  { value: "scroll_up", label: "Scroll Up" },
  { value: "scroll_down", label: "Scroll Down" },
]

interface KnobMapperProps {
  onPressSelect: (key: { id: string, label: string, value: string, width: number }) => void
  onRotateLeft: (action: string) => void
  onRotateRight: (action: string) => void
}

export default function KnobMapper({ onPressSelect, onRotateLeft, onRotateRight }: KnobMapperProps) {
  const [searchTerm, setSearchTerm] = useState("")
  const [rotateLeft, setRotateLeft] = useState<string>("")
  const [rotateRight, setRotateRight] = useState<string>("")
  const [pressKey, setPressKey] = useState<string>("")

  const groupedKeys = AllKeys.reduce((acc, key) => {
    if (!acc[key.type]) acc[key.type] = []
    acc[key.type].push(key)
    return acc
  }, {} as Record<string, typeof AllKeys>)

  const handleKeyClick = (id: string) => {
    const key = AllKeys.find(k => k.id === id)
    if (!key) return
    setPressKey(key.label)
    onPressSelect({ id: key.id, label: key.label, value: key.value, width: key.width })
  }

  return (
    <div className="flex gap-4 w-full h-full">

      {/* Gauche — 3 sélecteurs */}
      <div className="flex flex-col gap-4 w-1/2">

        {/* Rotate Left */}
        <div className="flex flex-col gap-1">
          <Label className="text-xs font-bold uppercase text-slate-600">Rotate Left</Label>
          <Select
            value={rotateLeft}
            onValueChange={(val) => {
              setRotateLeft(val as string)
              onRotateLeft(val as string)
            }}
          >
            <SelectTrigger className="w-full">
              <SelectValue placeholder="Select an action..." />
            </SelectTrigger>
            <SelectContent>
              {rotaryActions.map(action => (
                <SelectItem key={action.value} value={action.value}>
                  {action.label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>

        {/* Rotate Right */}
        <div className="flex flex-col gap-1">
          <Label className="text-xs font-bold uppercase text-slate-600">Rotate Right</Label>
          <Select
            value={rotateRight}
            onValueChange={(val) => {
              setRotateRight(val as string)
              onRotateRight(val as string)
            }}
          >
            <SelectTrigger className="w-full">
              <SelectValue placeholder="Select an action..." />
            </SelectTrigger>
            <SelectContent>
              {rotaryActions.map(action => (
                <SelectItem key={action.value} value={action.value}>
                  {action.label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>

        {/* Press */}
        <div className="flex flex-col gap-1">
          <Label className="text-xs font-bold uppercase text-slate-600">Press</Label>
          <Button variant="outline" className="w-full justify-start text-sm font-normal" disabled>
            {pressKey !== "" ? pressKey : "Select a key on the right →"}
          </Button>
        </div>

      </div>

      {/* Droite — search + touches */}
      <div className="flex flex-col gap-2 w-1/2">
        <Label className="text-xs font-bold uppercase text-slate-600">Keys</Label>
        <Input
          placeholder="Search..."
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
        />
        <ScrollArea className="h-45 rounded-md border p-2">
          {Object.entries(groupedKeys).map(([type, keys]) => (
            <Accordion key={type} defaultValue={["Basic"]} className="w-full">
              <AccordionItem value={type}>
                <AccordionTrigger>
                  <span className="text-xs font-bold uppercase text-slate-600">{type}</span>
                </AccordionTrigger>
                <AccordionContent>
                  {keys
                    .filter(k => k.id.toLowerCase().includes(searchTerm.toLowerCase()))
                    .map(keyData => (
                      <div className="inline-block m-0.5" key={keyData.id}>
                        <Key
                          id={keyData.id}
                          label={keyData.label}
                          width={keyData.width}
                          value={keyData.value}
                          onSelect={handleKeyClick}
                        />
                      </div>
                    ))}
                </AccordionContent>
              </AccordionItem>
            </Accordion>
          ))}
        </ScrollArea>
      </div>

    </div>
  )
}