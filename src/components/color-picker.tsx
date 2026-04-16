import { useCallback, useEffect, useRef, useState } from "react"
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover"
import { Button } from "@/components/ui/button"
import {
  ColorPicker as UIColorPicker,
  ColorPickerSelection,
  ColorPickerHue,
  ColorPickerOutput,
  ColorPickerFormat,
  type ColorArray,
} from "@/components/ui/color-picker"
import { Slider } from "@/components/ui/slider"
import { cn } from "@/lib/utils"

export interface RGBColor {
  r: number
  g: number
  b: number
}

interface ColorPickerProps {
  color: RGBColor
  onChange: (color: RGBColor) => void
  onLiveChange?: (color: RGBColor) => void
  className?: string
}

const PRESET_COLORS: RGBColor[] = [
  { r: 255, g: 0, b: 0 },
  { r: 255, g: 127, b: 0 },
  { r: 255, g: 255, b: 0 },
  { r: 0, g: 255, b: 0 },
  { r: 0, g: 255, b: 255 },
  { r: 0, g: 127, b: 255 },
  { r: 0, g: 0, b: 255 },
  { r: 127, g: 0, b: 255 },
  { r: 255, g: 0, b: 255 },
  { r: 255, g: 255, b: 255 },
  { r: 128, g: 128, b: 128 },
  { r: 0, g: 0, b: 0 },
]

const COLOR_COMMIT_TIMEOUT_MS = 1500

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value))
}

function clampByte(value: number): number {
  return clamp(Math.round(value), 0, 255)
}

function normalizeRgb(color: RGBColor): RGBColor {
  return {
    r: clampByte(color.r),
    g: clampByte(color.g),
    b: clampByte(color.b),
  }
}

function colorsEqual(a: RGBColor, b: RGBColor): boolean {
  return a.r === b.r && a.g === b.g && a.b === b.b
}

function rgbToHex(color: RGBColor): string {
  return `#${[color.r, color.g, color.b].map(v => v.toString(16).padStart(2, "0")).join("")}`
}

function rgbToRgbaArray(color: RGBColor): ColorArray {
  return [clampByte(color.r), clampByte(color.g), clampByte(color.b), 1]
}

function rgbaArrayToRgb(values: ColorArray): RGBColor {
  return {
    r: clampByte(values[0]),
    g: clampByte(values[1]),
    b: clampByte(values[2]),
  }
}

interface HSVColor {
  h: number
  s: number
  v: number
}

function normalizeHue(value: number): number {
  if (!Number.isFinite(value)) {
    return 0
  }
  const wrapped = ((value % 360) + 360) % 360
  return wrapped
}

function rgbToHsv(color: RGBColor): HSVColor {
  const r = clampByte(color.r) / 255
  const g = clampByte(color.g) / 255
  const b = clampByte(color.b) / 255

  const max = Math.max(r, g, b)
  const min = Math.min(r, g, b)
  const delta = max - min

  let hue = 0
  if (delta > 0) {
    if (max === r) {
      hue = ((g - b) / delta) % 6
    } else if (max === g) {
      hue = (b - r) / delta + 2
    } else {
      hue = (r - g) / delta + 4
    }
    hue *= 60
  }

  const saturation = max === 0 ? 0 : (delta / max) * 100
  const value = max * 100

  return {
    h: normalizeHue(hue),
    s: clamp(saturation, 0, 100),
    v: clamp(value, 0, 100),
  }
}

function hsvToRgb(hsv: HSVColor): RGBColor {
  const h = normalizeHue(hsv.h)
  const s = clamp(hsv.s, 0, 100) / 100
  const v = clamp(hsv.v, 0, 100) / 100

  const c = v * s
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1))
  const m = v - c

  let rPrime = 0
  let gPrime = 0
  let bPrime = 0

  if (h < 60) {
    rPrime = c
    gPrime = x
  } else if (h < 120) {
    rPrime = x
    gPrime = c
  } else if (h < 180) {
    gPrime = c
    bPrime = x
  } else if (h < 240) {
    gPrime = x
    bPrime = c
  } else if (h < 300) {
    rPrime = x
    bPrime = c
  } else {
    rPrime = c
    bPrime = x
  }

  return {
    r: clampByte((rPrime + m) * 255),
    g: clampByte((gPrime + m) * 255),
    b: clampByte((bPrime + m) * 255),
  }
}

export function ColorPicker({ color, onChange, onLiveChange, className }: ColorPickerProps) {
  const [open, setOpen] = useState(false)
  const [draftColor, setDraftColor] = useState<RGBColor | null>(null)
  const displayColor = draftColor ?? color
  const displayHsv = rgbToHsv(displayColor)

  const draftColorRef = useRef<RGBColor>(color)
  const pendingCommitRef = useRef<RGBColor | null>(null)
  const clearDraftTimerRef = useRef<number | null>(null)
  const pointerInteractionRef = useRef(false)
  const lastHueRef = useRef<number>(displayHsv.h)

  useEffect(() => {
    if (!draftColor) {
      draftColorRef.current = color
    }
  }, [color, draftColor])

  useEffect(() => {
    if (displayHsv.s > 0) {
      lastHueRef.current = displayHsv.h
    }
  }, [displayHsv.h, displayHsv.s])

  useEffect(() => {
    const pending = pendingCommitRef.current
    if (!pending || !colorsEqual(color, pending)) return

    pendingCommitRef.current = null
    setDraftColor(null)

    if (clearDraftTimerRef.current !== null) {
      window.clearTimeout(clearDraftTimerRef.current)
      clearDraftTimerRef.current = null
    }
  }, [color])

  useEffect(() => {
    return () => {
      if (clearDraftTimerRef.current !== null) {
        window.clearTimeout(clearDraftTimerRef.current)
      }
    }
  }, [])

  const queueCommitReset = useCallback(() => {
    if (clearDraftTimerRef.current !== null) {
      window.clearTimeout(clearDraftTimerRef.current)
    }

    clearDraftTimerRef.current = window.setTimeout(() => {
      pendingCommitRef.current = null
      setDraftColor(null)
      clearDraftTimerRef.current = null
    }, COLOR_COMMIT_TIMEOUT_MS)
  }, [])

  const pushLiveColor = useCallback(
    (next: RGBColor) => {
      const normalized = normalizeRgb(next)
      const current = draftColorRef.current
      if (colorsEqual(current, normalized)) return

      draftColorRef.current = normalized
      setDraftColor(normalized)
      onLiveChange?.(normalized)
    },
    [onLiveChange],
  )

  const commitColor = useCallback(
    (next: RGBColor) => {
      const normalized = normalizeRgb(next)
      const pending = pendingCommitRef.current
      if (pending && colorsEqual(pending, normalized)) return

      draftColorRef.current = normalized
      setDraftColor(normalized)
      pendingCommitRef.current = normalized
      queueCommitReset()
      onChange(normalized)
    },
    [onChange, queueCommitReset],
  )

  const commitCurrentDraft = useCallback(() => {
    const hasDraft = draftColor !== null || !colorsEqual(draftColorRef.current, color)
    if (!hasDraft) return
    const pending = pendingCommitRef.current
    if (pending && colorsEqual(pending, draftColorRef.current)) return

    commitColor(draftColorRef.current)
  }, [color, commitColor, draftColor])

  const handlePickerChange = useCallback(
    (next: ColorArray) => {
      pushLiveColor(rgbaArrayToRgb(next))
    },
    [pushLiveColor],
  )

  const handlePresetClick = useCallback(
    (next: RGBColor) => {
      pushLiveColor(next)
      commitColor(next)
    },
    [pushLiveColor, commitColor],
  )

  const handleSaturationChange = useCallback(
    (value: number | readonly number[]) => {
      const nextSaturation = typeof value === "number" ? value : value[0]
      if (!Number.isFinite(nextSaturation)) {
        return
      }

      const currentHsv = rgbToHsv(draftColorRef.current)
      const hue = currentHsv.s > 0 ? currentHsv.h : lastHueRef.current
      if (nextSaturation > 0) {
        lastHueRef.current = hue
      }

      pushLiveColor(
        hsvToRgb({
          h: hue,
          s: nextSaturation,
          v: currentHsv.v,
        }),
      )
    },
    [pushLiveColor],
  )

  const handleBrightnessChange = useCallback(
    (value: number | readonly number[]) => {
      const nextBrightness = typeof value === "number" ? value : value[0]
      if (!Number.isFinite(nextBrightness)) {
        return
      }

      const currentHsv = rgbToHsv(draftColorRef.current)
      const hue = currentHsv.s > 0 ? currentHsv.h : lastHueRef.current
      if (currentHsv.s > 0) {
        lastHueRef.current = hue
      }

      pushLiveColor(
        hsvToRgb({
          h: hue,
          s: currentHsv.s,
          v: nextBrightness,
        }),
      )
    },
    [pushLiveColor],
  )

  const handleOpenChange = useCallback(
    (nextOpen: boolean) => {
      if (!nextOpen) {
        pointerInteractionRef.current = false
        commitCurrentDraft()
      }
      setOpen(nextOpen)
    },
    [commitCurrentDraft],
  )

  const handlePointerDownCapture = useCallback(() => {
    pointerInteractionRef.current = true
  }, [])

  const handlePointerRelease = useCallback(() => {
    if (!pointerInteractionRef.current) return
    pointerInteractionRef.current = false
    commitCurrentDraft()
  }, [commitCurrentDraft])

  return (
    <Popover open={open} onOpenChange={handleOpenChange}>
      <PopoverTrigger
        render={
          <Button variant="outline" className={cn("h-8 w-16 border p-0", className)}>
            <div
              className="h-full w-full rounded-[calc(var(--radius)-4px)]"
              style={{ backgroundColor: rgbToHex(displayColor) }}
            />
          </Button>
        }
      />

      <PopoverContent
        className="w-72"
        align="start"
        onPointerDownCapture={handlePointerDownCapture}
        onPointerUp={handlePointerRelease}
        onPointerCancel={handlePointerRelease}
        onLostPointerCapture={handlePointerRelease}
        onKeyUpCapture={event => {
          if (event.key === "Enter") {
            commitCurrentDraft()
          }
        }}
      >
        <div className="flex flex-col gap-3">
          <UIColorPicker
            value={rgbToRgbaArray(displayColor)}
            onChange={handlePickerChange}
            className="h-auto w-full"
          >
            <div className="rounded-md border p-px">
              <ColorPickerSelection className="h-32 rounded-[calc(var(--radius)-3px)]" />
            </div>
            <ColorPickerHue />
            <div className="grid gap-2">
              <div className="grid gap-1">
                <div className="flex items-center justify-between text-xs text-muted-foreground">
                  <span>Saturation</span>
                  <span className="font-mono tabular-nums">{Math.round(displayHsv.s)}%</span>
                </div>
                <Slider
                  min={0}
                  max={100}
                  step={1}
                  value={[Math.round(displayHsv.s)]}
                  onValueChange={handleSaturationChange}
                />
              </div>
              <div className="grid gap-1">
                <div className="flex items-center justify-between text-xs text-muted-foreground">
                  <span>Brightness</span>
                  <span className="font-mono tabular-nums">{Math.round(displayHsv.v)}%</span>
                </div>
                <Slider
                  min={0}
                  max={100}
                  step={1}
                  value={[Math.round(displayHsv.v)]}
                  onValueChange={handleBrightnessChange}
                />
              </div>
            </div>
            <div className="flex items-center gap-2">
              <ColorPickerOutput />
              <ColorPickerFormat className="min-w-0 flex-1" />
            </div>
          </UIColorPicker>

          <div className="flex flex-wrap gap-1">
            {PRESET_COLORS.map((preset, index) => (
              <button
                key={index}
                type="button"
                aria-label={`Select ${rgbToHex(preset)} preset`}
                className={cn(
                  "size-6 rounded-md border transition-transform hover:scale-110",
                  rgbToHex(preset) === rgbToHex(displayColor) && "ring-2 ring-primary ring-offset-1",
                )}
                style={{ backgroundColor: rgbToHex(preset) }}
                onClick={() => handlePresetClick(preset)}
              />
            ))}
          </div>
        </div>
      </PopoverContent>
    </Popover>
  )
}
