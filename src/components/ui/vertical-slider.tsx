import * as React from "react"
import { Slider as SliderPrimitive } from "@base-ui/react/slider"
import { cn } from "@/lib/utils"

interface VerticalSliderProps {
  value: number[]
  onChange: (value: number[]) => void
  min?: number
  max?: number
  step?: number
  disabled?: boolean
  className?: string
}

export default function VerticalSlider({
  value,
  onChange,
  min = 0,
  max = 100,
  step = 1,
  disabled = false,
  className,
}: VerticalSliderProps) {
  const steps = Math.floor((max - min) / step)
  const graduations = Array.from({ length: steps + 1 }, (_, i) => max - i * step)

  return (
    <div className={cn("relative h-80 w-32 flex items-start justify-center pt-8", className)}>
      {/* Graduations top */}
      <div className="absolute top-0 left-0 w-full h-8 flex justify-between px-2 pointer-events-none">
        {graduations.map((grad, idx) => (
          <div
            key={idx}
            className="flex flex-col items-center"
          >
            <div className="w-0.5 h-1.5 bg-muted-foreground"></div>
            <span className="text-[9px] text-muted-foreground mt-0.5 font-medium">{grad.toFixed(1)}</span>
          </div>
        ))}
      </div>

      {/* Slider */}
      <div className="w-full">
        <SliderPrimitive.Root
          className="w-full h-auto"
          orientation="vertical"
          value={value}
          onValueChange={onChange}
          min={min}
          max={max}
          step={step}
          disabled={disabled}
          thumbAlignment="center"
        >
          <SliderPrimitive.Control className="relative h-64 w-full flex justify-center touch-none select-none">
            <SliderPrimitive.Track
              data-slot="slider-track"
              className="bg-gradient-to-b from-gray-300 via-blue-400 to-gray-300 rounded-lg h-full w-4 relative overflow-hidden select-none shadow-md"
            >
              <SliderPrimitive.Indicator
                data-slot="slider-range"
                className="bg-gradient-to-b from-blue-600 via-cyan-400 to-blue-500 select-none w-full shadow-lg"
              />
            </SliderPrimitive.Track>
            <SliderPrimitive.Thumb
              data-slot="slider-thumb"
              className="absolute relative w-6 h-6 rounded-full border-2 border-gray-400 bg-white shadow-lg transition-all duration-200 hover:shadow-xl focus-visible:ring-2 focus-visible:ring-blue-400 outline-hidden shrink-0 select-none disabled:opacity-50 cursor-grab active:cursor-grabbing -left-1"
            />
          </SliderPrimitive.Control>
        </SliderPrimitive.Root>
      </div>
    </div>
  )
}
