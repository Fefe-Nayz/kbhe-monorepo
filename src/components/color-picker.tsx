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

export function ColorPicker({ color, onChange, onLiveChange, className }: ColorPickerProps) {
  const [open, setOpen] = useState(false)
  const [draftColor, setDraftColor] = useState<RGBColor | null>(null)
  const displayColor = draftColor ?? color

  const draftColorRef = useRef<RGBColor>(color)
  const pendingCommitRef = useRef<RGBColor | null>(null)
  const clearDraftTimerRef = useRef<number | null>(null)
  const pointerInteractionRef = useRef(false)

  useEffect(() => {
    if (!draftColor) {
      draftColorRef.current = color
    }
  }, [color, draftColor])

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
