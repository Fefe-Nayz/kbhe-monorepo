"use client"

import Color from "color"
import { PipetteIcon } from "lucide-react"
import { Slider as SliderPrimitive } from "@base-ui/react/slider"
import {
  type ComponentProps,
  createContext,
  type HTMLAttributes,
  memo,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { cn } from "@/lib/utils"

interface ColorPickerContextValue {
  hue: number
  saturation: number
  lightness: number
  alpha: number
  mode: string
  setHue: (hue: number) => void
  setSaturation: (saturation: number) => void
  setLightness: (lightness: number) => void
  setAlpha: (alpha: number) => void
  setMode: (mode: string) => void
}

const ColorPickerContext = createContext<ColorPickerContextValue | undefined>(undefined)

export const useColorPicker = () => {
  const context = useContext(ColorPickerContext)

  if (!context) {
    throw new Error("useColorPicker must be used within a ColorPickerProvider")
  }

  return context
}

export type ColorPickerProps = Omit<HTMLAttributes<HTMLDivElement>, "onChange"> & {
  value?: Parameters<typeof Color>[0]
  defaultValue?: Parameters<typeof Color>[0]
  onChange?: (value: ColorArray) => void
}

export type ColorArray = [number, number, number, number]

export const ColorPicker = ({
  value,
  defaultValue = "#000000",
  onChange,
  className,
  ...props
}: ColorPickerProps) => {
  const selectedColor = Color(value ?? defaultValue)
  const defaultColor = Color(defaultValue)
  const controlledColorRef = useRef<string>("")
  const emittedColorRef = useRef<string>("")

  const [hue, setHue] = useState(selectedColor.hue() || defaultColor.hue() || 0)
  const [saturation, setSaturation] = useState(
    selectedColor.saturationl() || defaultColor.saturationl() || 100,
  )
  const [lightness, setLightness] = useState(
    selectedColor.lightness() || defaultColor.lightness() || 50,
  )
  const [alpha, setAlpha] = useState(selectedColor.alpha() * 100 || defaultColor.alpha() * 100)
  const [mode, setMode] = useState("hex")

  // Update color when controlled value changes
  useEffect(() => {
    if (value) {
      const source = Color(value)
      const rgba = source.rgb().array()
      const echoedKey = `${Math.round(rgba[0])}:${Math.round(rgba[1])}:${Math.round(rgba[2])}:${Number(source.alpha().toFixed(3))}`

      if (emittedColorRef.current === echoedKey) {
        return
      }

      const color = source.hsl()
      const [nextHue, nextSaturation, nextLightness] = color.array()
      const nextAlpha = Math.round(color.alpha() * 100)
      const key = `${Math.round(nextHue * 1000)}:${Math.round(nextSaturation * 1000)}:${Math.round(nextLightness * 1000)}:${nextAlpha}`

      if (controlledColorRef.current === key) {
        return
      }

      controlledColorRef.current = key

      setHue(Number.isFinite(nextHue) ? nextHue : 0)
      setSaturation(Number.isFinite(nextSaturation) ? nextSaturation : 100)
      setLightness(Number.isFinite(nextLightness) ? nextLightness : 50)
      setAlpha(nextAlpha)
    }
  }, [value])

  // Notify parent of changes
  useEffect(() => {
    if (onChange) {
      const color = Color.hsl(hue, saturation, lightness).alpha(alpha / 100)
      const rgba = color.rgb().array()
      const rounded = [
        Math.round(rgba[0]),
        Math.round(rgba[1]),
        Math.round(rgba[2]),
        Number((alpha / 100).toFixed(3)),
      ] as [number, number, number, number]
      const key = `${rounded[0]}:${rounded[1]}:${rounded[2]}:${rounded[3]}`

      if (emittedColorRef.current === key) {
        return
      }

      emittedColorRef.current = key
      onChange(rounded)
    }
  }, [hue, saturation, lightness, alpha, onChange])

  return (
    <ColorPickerContext.Provider
      value={{
        hue,
        saturation,
        lightness,
        alpha,
        mode,
        setHue,
        setSaturation,
        setLightness,
        setAlpha,
        setMode,
      }}
    >
      <div className={cn("flex size-full flex-col gap-4", className)} {...(props as any)} />
    </ColorPickerContext.Provider>
  )
}

export type ColorPickerSelectionProps = HTMLAttributes<HTMLDivElement>

export const ColorPickerSelection = memo(({ className, ...props }: ColorPickerSelectionProps) => {
  const containerRef = useRef<HTMLDivElement>(null)
  const isDraggingRef = useRef(false)
  const [positionX, setPositionX] = useState(0)
  const [positionY, setPositionY] = useState(0)
  const { hue, saturation, lightness, setSaturation, setLightness } = useColorPicker()

  const backgroundGradient = useMemo(() => {
    return `linear-gradient(0deg, rgba(0,0,0,1), rgba(0,0,0,0)),
            linear-gradient(90deg, rgba(255,255,255,1), rgba(255,255,255,0)),
            hsl(${hue}, 100%, 50%)`
  }, [hue])

  useEffect(() => {
    if (isDraggingRef.current) {
      return
    }

    const hsv = Color.hsl(hue, saturation, lightness).hsv().array()
    const sat = Math.max(0, Math.min(100, Number(hsv[1]) || 0))
    const value = Math.max(0, Math.min(100, Number(hsv[2]) || 0))

    setPositionX(sat / 100)
    setPositionY(1 - value / 100)
  }, [hue, saturation, lightness])

  const updateFromPointer = useCallback(
    (event: PointerEvent) => {
      if (!containerRef.current) {
        return
      }

      const rect = containerRef.current.getBoundingClientRect()
      const styles = window.getComputedStyle(containerRef.current)
      const borderLeft = Number.parseFloat(styles.borderLeftWidth) || 0
      const borderRight = Number.parseFloat(styles.borderRightWidth) || 0
      const borderTop = Number.parseFloat(styles.borderTopWidth) || 0
      const borderBottom = Number.parseFloat(styles.borderBottomWidth) || 0

      const innerWidth = Math.max(1, rect.width - borderLeft - borderRight)
      const innerHeight = Math.max(1, rect.height - borderTop - borderBottom)
      const x = Math.max(
        0,
        Math.min(1, (event.clientX - rect.left - borderLeft) / innerWidth),
      )
      const y = Math.max(
        0,
        Math.min(1, (event.clientY - rect.top - borderTop) / innerHeight),
      )

      setPositionX(x)
      setPositionY(y)

      const hsvColor = Color.hsv(hue, x * 100, (1 - y) * 100)
      const [, nextSaturation, nextLightness] = hsvColor.hsl().array()

      setSaturation(Number.isFinite(nextSaturation) ? nextSaturation : 100)
      setLightness(Number.isFinite(nextLightness) ? nextLightness : 50)
    },
    [hue, setSaturation, setLightness],
  )

  const handlePointerDown = useCallback(
    (event: React.PointerEvent<HTMLDivElement>) => {
      event.preventDefault()
      isDraggingRef.current = true
      event.currentTarget.setPointerCapture(event.pointerId)
      updateFromPointer(event.nativeEvent)
    },
    [updateFromPointer],
  )

  const handlePointerMove = useCallback(
    (event: React.PointerEvent<HTMLDivElement>) => {
      if (!isDraggingRef.current) {
        return
      }

      updateFromPointer(event.nativeEvent)
    },
    [updateFromPointer],
  )

  const handlePointerUp = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    isDraggingRef.current = false
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId)
    }
  }, [])

  return (
    <div
      className={cn("relative size-full touch-none select-none cursor-crosshair rounded", className)}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      onLostPointerCapture={handlePointerUp}
      ref={containerRef}
      style={{
        background: backgroundGradient,
      }}
      {...(props as any)}
    >
      <div
        className="-translate-x-1/2 -translate-y-1/2 pointer-events-none absolute h-4 w-4 rounded-full border-2 border-white"
        style={{
          left: `${positionX * 100}%`,
          top: `${positionY * 100}%`,
          boxShadow: "0 0 0 1px rgba(0,0,0,0.5)",
        }}
      />
    </div>
  )
})

ColorPickerSelection.displayName = "ColorPickerSelection"

export type ColorPickerHueProps = ComponentProps<typeof SliderPrimitive.Root>

export const ColorPickerHue = ({ className, ...props }: ColorPickerHueProps) => {
  const { hue, setHue } = useColorPicker()

  return (
    <SliderPrimitive.Root
      className={cn("relative flex h-4 w-full touch-none", className)}
      max={360}
      onValueChange={(value: number | readonly number[]) => {
        const next = typeof value === "number" ? value : value[0]
        if (next !== undefined) {
          setHue(next)
        }
      }}
      step={1}
      value={[hue]}
      {...(props as any)}
    >
      <SliderPrimitive.Control className="relative flex w-full touch-none items-center select-none data-disabled:opacity-50">
        <SliderPrimitive.Track className="relative my-0.5 h-3 w-full grow overflow-hidden rounded-full bg-[linear-gradient(90deg,#FF0000,#FFFF00,#00FF00,#00FFFF,#0000FF,#FF00FF,#FF0000)]">
          <SliderPrimitive.Indicator className="absolute h-full" />
        </SliderPrimitive.Track>
        <SliderPrimitive.Thumb className="relative block h-4 w-4 shrink-0 rounded-full border border-primary/50 bg-background shadow transition-colors focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50" />
      </SliderPrimitive.Control>
    </SliderPrimitive.Root>
  )
}

export type ColorPickerAlphaProps = ComponentProps<typeof SliderPrimitive.Root>

export const ColorPickerAlpha = ({ className, ...props }: ColorPickerAlphaProps) => {
  const { alpha, setAlpha } = useColorPicker()

  return (
    <SliderPrimitive.Root
      className={cn("relative flex h-4 w-full touch-none", className)}
      max={100}
      onValueChange={(value: number | readonly number[]) => {
        const next = typeof value === "number" ? value : value[0]
        if (next !== undefined) {
          setAlpha(next)
        }
      }}
      step={1}
      value={[alpha]}
      {...(props as any)}
    >
      <SliderPrimitive.Control className="relative flex w-full touch-none items-center select-none data-disabled:opacity-50">
        <SliderPrimitive.Track
          className="relative my-0.5 h-3 w-full grow overflow-hidden rounded-full"
          style={{
            background:
              'url("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAMUlEQVQ4T2NkYGAQYcAP3uCTZhw1gGGYhAGBZIA/nYDCgBDAm9BGDWAAJyRCgLaBCAAgXwixzAS0pgAAAABJRU5ErkJggg==") left center',
          }}
        >
          <div className="absolute inset-0 rounded-full bg-linear-to-r from-transparent to-black/50" />
          <SliderPrimitive.Indicator className="absolute h-full rounded-full bg-transparent" />
        </SliderPrimitive.Track>
        <SliderPrimitive.Thumb className="relative block h-4 w-4 shrink-0 rounded-full border border-primary/50 bg-background shadow transition-colors focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50" />
      </SliderPrimitive.Control>
    </SliderPrimitive.Root>
  )
}

export type ColorPickerEyeDropperProps = ComponentProps<typeof Button>

export const ColorPickerEyeDropper = ({ className, ...props }: ColorPickerEyeDropperProps) => {
  const { setHue, setSaturation, setLightness, setAlpha } = useColorPicker()

  const handleEyeDropper = async () => {
    try {
      // @ts-expect-error - EyeDropper API is experimental
      const eyeDropper = new EyeDropper()
      const result = await eyeDropper.open()
      const color = Color(result.sRGBHex)
      const [h, s, l] = color.hsl().array()

      setHue(h)
      setSaturation(s)
      setLightness(l)
      setAlpha(100)
    } catch (error) {
      console.error("EyeDropper failed:", error)
    }
  }

  return (
    <Button
      className={cn("shrink-0 text-muted-foreground", className)}
      onClick={handleEyeDropper}
      size="icon"
      type="button"
      variant="outline"
      {...(props as any)}
    >
      <PipetteIcon size={16} />
    </Button>
  )
}

export type ColorPickerOutputProps = ComponentProps<typeof SelectTrigger>

const formats = ["hex", "rgb", "css", "hsl"]

export const ColorPickerOutput = ({ className, ...props }: ColorPickerOutputProps) => {
  const { mode, setMode } = useColorPicker()

  return (
    <Select
      onValueChange={(value) => {
        if (value) {
          setMode(value)
        }
      }}
      value={mode}
    >
      <SelectTrigger className="h-8 w-20 shrink-0 text-xs" {...(props as any)}>
        <SelectValue placeholder="Mode" />
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          {formats.map(format => (
            <SelectItem className="text-xs" key={format} value={format}>
              {format.toUpperCase()}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </Select>
  )
}

type PercentageInputProps = ComponentProps<typeof Input>

const PercentageInput = ({ className, ...props }: PercentageInputProps) => {
  return (
    <div className="relative">
      <Input
        readOnly
        type="text"
        {...(props as any)}
        className={cn(
          "h-8 w-13 rounded-l-none bg-secondary px-2 text-xs shadow-none",
          className,
        )}
      />
      <span className="-translate-y-1/2 absolute top-1/2 right-2 text-muted-foreground text-xs">
        %
      </span>
    </div>
  )
}

export type ColorPickerFormatProps = HTMLAttributes<HTMLDivElement>

export const ColorPickerFormat = ({ className, ...props }: ColorPickerFormatProps) => {
  const { hue, saturation, lightness, alpha, mode } = useColorPicker()
  const color = Color.hsl(hue, saturation, lightness, alpha / 100)

  if (mode === "hex") {
    const hex = color.hex()

    return (
      <div
        className={cn(
          "-space-x-px relative flex w-full items-center rounded-md shadow-sm",
          className,
        )}
        {...(props as any)}
      >
        <Input
          className="h-8 rounded-r-none bg-secondary px-2 text-xs shadow-none"
          readOnly
          type="text"
          value={hex}
        />
        <PercentageInput value={alpha} />
      </div>
    )
  }

  if (mode === "rgb") {
    const rgb = color
      .rgb()
      .array()
      .map((value: number) => Math.round(value))

    return (
      <div
        className={cn("-space-x-px flex items-center rounded-md shadow-sm", className)}
        {...(props as any)}
      >
        {rgb.map((value: number, index: number) => (
          <Input
            className={cn(
              "h-8 rounded-r-none bg-secondary px-2 text-xs shadow-none",
              index && "rounded-l-none",
              className,
            )}
            key={index}
            readOnly
            type="text"
            value={value}
          />
        ))}
        <PercentageInput value={alpha} />
      </div>
    )
  }

  if (mode === "css") {
    const rgb = color
      .rgb()
      .array()
      .map((value: number) => Math.round(value))

    return (
      <div className={cn("w-full rounded-md shadow-sm", className)} {...(props as any)}>
        <Input
          className="h-8 w-full bg-secondary px-2 text-xs shadow-none"
          readOnly
          type="text"
          value={`rgba(${rgb.join(", ")}, ${alpha}%)`}
          {...(props as any)}
        />
      </div>
    )
  }

  if (mode === "hsl") {
    const hsl = color
      .hsl()
      .array()
      .map((value: number) => Math.round(value))

    return (
      <div
        className={cn("-space-x-px flex items-center rounded-md shadow-sm", className)}
        {...(props as any)}
      >
        {hsl.map((value: number, index: number) => (
          <Input
            className={cn(
              "h-8 rounded-r-none bg-secondary px-2 text-xs shadow-none",
              index && "rounded-l-none",
              className,
            )}
            key={index}
            readOnly
            type="text"
            value={value}
          />
        ))}
        <PercentageInput value={alpha} />
      </div>
    )
  }

  return null
}

// Demo
export function Demo() {
  return (
    <div className="fixed inset-0 flex items-center justify-center p-8">
      <ColorPicker defaultValue="#6366f1" className="h-auto w-64">
        <ColorPickerSelection className="h-40 rounded-lg" />
        <ColorPickerHue />
        <ColorPickerAlpha />
        <div className="flex items-center gap-2">
          <ColorPickerEyeDropper />
          <ColorPickerOutput />
          <ColorPickerFormat />
        </div>
      </ColorPicker>
    </div>
  )
}
