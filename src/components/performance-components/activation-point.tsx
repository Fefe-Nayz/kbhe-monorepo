import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import SliderVertical from "@/components/performance-components/slider-vertical"

interface ActivationPointCardProps {
  value: number
  onChange: (value: number) => void
}

export default function ActivationPointCard({ value, onChange }: ActivationPointCardProps) {
  return (
    <Card className="w-48 rounded-2xl border border-border bg-background shadow-sm shrink-0">
      <CardHeader className="pb-1">
        <CardTitle className="text-sm">Activation Point</CardTitle>
        <CardDescription className="text-xs">0.2 mm — 3 mm</CardDescription>
      </CardHeader>
      <CardContent className="flex flex-col items-center gap-2">
        <div className="flex items-center justify-between text-xs text-muted-foreground w-full">
          <span>Distance</span>
          <span className="font-semibold text-foreground">{value.toFixed(1)} mm</span>
        </div>
        <div className="h-28 flex items-center justify-center">
          <SliderVertical value={value} onChange={onChange} />
        </div>
      </CardContent>
    </Card>
  )
}