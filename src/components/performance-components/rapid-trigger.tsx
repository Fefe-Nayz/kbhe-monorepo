import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import * as React from "react"
import { Slider as SliderPrimitive } from "@base-ui/react/slider"
import { Switch } from "@/components/ui/switch"

interface RapidTriggerCardProps {
  value: number
  enabled: boolean
  onToggle: () => void
  onChange: (value: number) => void
}

export default function RapidTriggerCard({ value, enabled, onToggle, onChange }: RapidTriggerCardProps) {
  return (
    <Card className="w-full sm:w-80 md:w-96 lg:w-96 xl:w-[500px] 2xl:w-[600px] rounded-3xl border border-border bg-background shadow-sm">
      <CardHeader className="pb-4 flex items-center justify-between flex-row">
        <div>
          <CardTitle className="text-base">Rapid Trigger</CardTitle>
          <CardDescription className="text-sm">Adjust sensitivity when toggled.</CardDescription>
        </div>
        <Switch checked={enabled} onCheckedChange={onToggle} />
      </CardHeader>
      <CardContent className="space-y-4 flex flex-col items-center">
        <div className="flex items-center justify-between text-sm text-muted-foreground w-full">
          <span>Value</span>
          <span className="font-semibold text-foreground">{value.toFixed(1)}</span>
        </div>
        <div className="h-40 flex items-center justify-center">
          <SliderPrimitive.Root
            className="h-full"
            orientation="vertical"
            value={[value]}
            onValueChange={(newValue) => {
              const next = Array.isArray(newValue) ? newValue[0] : newValue
              onChange(Number(next.toFixed(1)))
            }}
            min={0.2}
            max={3}
            step={0.1}
            disabled={!enabled}
            thumbAlignment="center"
          >
            <SliderPrimitive.Control className="relative h-full w-160 touch-none select-none flex justify-center">
              <SliderPrimitive.Track
                data-slot="slider-track"
                className="bg-muted border border-slate-300 rounded-sm h-full w-11 relative overflow-hidden select-none"
              >
                <SliderPrimitive.Indicator
                  data-slot="slider-range"
                  className="bg-primary select-none w-full"
                />
              </SliderPrimitive.Track>
              <SliderPrimitive.Thumb
                data-slot="slider-thumb"
                className="block h-2 w-10 border-ring ring-ring/50 relative size-5 rounded-sm border bg-white transition-[color,box-shadow] hover:ring-[3px] focus-visible:ring-[3px] outline-hidden shrink-0 select-none disabled:opacity-50 shadow-md"
              />
            </SliderPrimitive.Control>
          </SliderPrimitive.Root>
        </div>
      </CardContent>
    </Card>
  )
}
