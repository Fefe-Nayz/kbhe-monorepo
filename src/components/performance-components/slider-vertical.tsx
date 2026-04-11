import { Slider as SliderPrimitive } from "@base-ui/react/slider"

interface SliderVerticalProps {
  value: number
  min?: number
  max?: number
  step?: number
  disabled?: boolean
  onChange: (value: number) => void
}

export default function SliderVertical({
  value,
  min = 0.2,
  max = 3,
  step = 0.1,
  disabled = false,
  onChange
}: SliderVerticalProps) {
  return (
    <SliderPrimitive.Root
      className="h-full"
      orientation="vertical"
      value={[value]}
      onValueChange={(newValue) => {
        const next = Array.isArray(newValue) ? newValue[0] : newValue
        onChange(Number(next.toFixed(1)))
      }}
      min={min}
      max={max}
      step={step}
      disabled={disabled}
      thumbAlignment="center"
    >
      <SliderPrimitive.Control className="relative h-full w-16 touch-none select-none flex justify-center">
        <SliderPrimitive.Track className="bg-muted rounded-sm border border-slate-300 h-full w-11 relative overflow-hidden select-none">
          <SliderPrimitive.Indicator className="bg-primary select-none w-full" />
        </SliderPrimitive.Track>
        <SliderPrimitive.Thumb className="block border-ring ring-ring/50 relative h-2 size-11 rounded-sm border bg-white transition-[color,box-shadow] hover:ring-[3px] focus-visible:ring-[3px] outline-hidden shrink-0 select-none disabled:opacity-50 shadow-md" />
      </SliderPrimitive.Control>
    </SliderPrimitive.Root>
  )
}