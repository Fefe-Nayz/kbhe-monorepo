import { useCallback, useState, useRef, useEffect } from "react"
import { Slider as SliderPrimitive } from "@base-ui/react/slider"

import { cn } from "@/lib/utils"

function Slider({
  className,
  defaultValue,
  value,
  min = 0,
  max = 100,
  ...props
}: SliderPrimitive.Root.Props) {
  const _values = Array.isArray(value)
    ? value
    : Array.isArray(defaultValue)
      ? defaultValue
      : [min, max]

  return (
    <SliderPrimitive.Root
      className={cn("data-horizontal:w-full data-vertical:h-full", className)}
      data-slot="slider"
      defaultValue={defaultValue}
      value={value}
      min={min}
      max={max}
      thumbAlignment="edge"
      {...props}
    >
      <SliderPrimitive.Control className="relative flex w-full touch-none items-center select-none data-disabled:opacity-50 data-vertical:h-full data-vertical:min-h-40 data-vertical:w-auto data-vertical:flex-col">
        <SliderPrimitive.Track
          data-slot="slider-track"
          className="relative grow overflow-hidden rounded-full bg-muted select-none data-horizontal:h-1 data-horizontal:w-full data-vertical:h-full data-vertical:w-1"
        >
          <SliderPrimitive.Indicator
            data-slot="slider-range"
            className="bg-primary select-none data-horizontal:h-full data-vertical:w-full"
          />
        </SliderPrimitive.Track>
        {Array.from({ length: _values.length }, (_, index) => (
          <SliderPrimitive.Thumb
            data-slot="slider-thumb"
            key={index}
            className="relative block size-3 shrink-0 rounded-full border border-ring bg-white ring-ring/50 transition-[color,box-shadow] select-none after:absolute after:-inset-2 hover:ring-3 focus-visible:ring-3 focus-visible:outline-hidden active:ring-3 disabled:pointer-events-none disabled:opacity-50"
          />
        ))}
      </SliderPrimitive.Control>
    </SliderPrimitive.Root>
  )
}

/**
 * Slider that keeps local state during drag and only fires `onCommit`
 * when the pointer is released. The UI updates instantly with zero lag.
 *
 * While not dragging, `value` prop drives the display.
 * During drag, an internal draft overrides it for instant feedback.
 *
 * After pointer-up, draft is kept alive until the `value` prop catches up to
 * the committed value (i.e. the parent's optimistic update has arrived).
 * A 1.5 s fallback timeout clears it regardless, so draft never gets stuck.
 */
function CommitSlider({
  value,
  onCommit,
  onLiveChange,
  className,
  ...props
}: Omit<SliderPrimitive.Root.Props, "value" | "onValueChange" | "className"> & {
  value: number
  onCommit: (value: number) => void
  onLiveChange?: (value: number) => void
  className?: string
}) {
  const [draft, setDraft] = useState<number | null>(null)
  const commitRef = useRef<number>(value)
  // After pointer-up, holds the value we're waiting for the prop to match.
  const waitingForRef = useRef<number | null>(null)
  const clearTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  const displayValue = draft ?? value

  // Clear draft as soon as the value prop catches up to the committed value.
  useEffect(() => {
    if (waitingForRef.current === null) return
    if (value === waitingForRef.current) {
      setDraft(null)
      waitingForRef.current = null
      if (clearTimerRef.current) {
        clearTimeout(clearTimerRef.current)
        clearTimerRef.current = null
      }
    }
  }, [value])

  const handleChange = useCallback(
    (v: number | readonly number[]) => {
      const n = typeof v === "number" ? v : (v as readonly number[])[0]
      if (n === undefined) return
      setDraft(n)
      commitRef.current = n
      onLiveChange?.(n)
    },
    [onLiveChange],
  )

  const handlePointerDown = useCallback(() => {
    setDraft(value)
    commitRef.current = value
    waitingForRef.current = null
    if (clearTimerRef.current) {
      clearTimeout(clearTimerRef.current)
      clearTimerRef.current = null
    }
  }, [value])

  const handlePointerUp = useCallback(() => {
    if (draft === null) return
    const final = commitRef.current
    // Keep draft alive until value prop matches — no flash.
    waitingForRef.current = final
    // Fallback: always clear after 1.5 s (handles mutation errors / device rounding).
    if (clearTimerRef.current) clearTimeout(clearTimerRef.current)
    clearTimerRef.current = setTimeout(() => {
      setDraft(null)
      waitingForRef.current = null
      clearTimerRef.current = null
    }, 1500)
    onCommit(final)
  }, [draft, onCommit])

  return (
    <div
      className={className}
      onPointerDown={handlePointerDown}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      onLostPointerCapture={handlePointerUp}
    >
      <Slider
        value={[displayValue]}
        onValueChange={handleChange}
        {...props}
      />
    </div>
  )
}

export { Slider, CommitSlider }
