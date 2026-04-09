"use client"
import { useState } from "react"
import { Card, CardContent } from "@/components/ui/card"
import { HexColorPicker, HexColorInput } from "react-colorful"

export default function ColorPickerCard() {
  const [color, setColor] = useState("#3b82f6")

  function hexToRgb(hex: string) {
    const r = parseInt(hex.slice(1, 3), 16)
    const g = parseInt(hex.slice(3, 5), 16)
    const b = parseInt(hex.slice(5, 7), 16)
    return { r, g, b }
  }

  const rgb = hexToRgb(color)

  return (
    <Card className="bg-blue-600 w-68 rounded-2xl border border-border shadow-sm shrink-0 pb-0 pt-0">
      {/*<CardHeader className="pb-1 bg-black" />*/}

      <CardContent className="p-2 space-y-1 bg-red-600 h-fit">
        {/* Color picker centré */}
        <div className="flex justify-center transform scale-90 bg-amber-500">
          <HexColorPicker color={color} onChange={setColor} className="w-48 h-32 rounded-md" />
        </div>

        {/* HEX input + preview */}
        <div className="flex items-center gap-2 bg-green-900">
          <div
            className="w-8 h-8 rounded border"
            style={{ background: color }}
          />
          <HexColorInput
            color={color}
            onChange={setColor}
            className="w-full text-sm px-2 py-1 border rounded bg-background"
          />
        </div>

        {/* HEX & RGB values */}
        <div className="grid grid-cols-2 gap-2 text-xs">
          <div className="flex justify-between px-2 py-1 border rounded">
            <span className="text-muted-foreground">HEX</span>
            <span className="font-medium">{color}</span>
          </div>

          <div className="flex justify-between px-2 py-1 border rounded">
            <span className="text-muted-foreground">RGB</span>
            <span className="font-medium">{rgb.r}, {rgb.g}, {rgb.b}</span>
          </div>
        </div>
      </CardContent>
    </Card>
  )
}