
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Slider } from "@/components/ui/slider"

interface ActivationPointCardProps {
  value: number
  onChange: (value: number) => void
}

export default function ActivationPointCard({ value, onChange }: ActivationPointCardProps) {
  return (
    <Card className="w-125 rounded-3xl border border-border bg-background shadow-sm">
      <CardHeader>
        <div>
          <CardTitle>Activation Point</CardTitle>
          <CardDescription>Between 0.2 mm and 3 mm.</CardDescription>
        </div>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex items-center justify-between text-sm text-muted-foreground">
          <span>Current Distance</span>
          <span className="font-semibold text-foreground">{value.toFixed(1)} mm</span>
        </div>
        <Slider
          value={[value]}
          min={0.2}
          max={3}
          step={0.1}
          onValueChange={(newValue) => {
            const next = Array.isArray(newValue) ? newValue[0] : newValue
            onChange(Number(next.toFixed(1)))
          }}
        />
      </CardContent>
    </Card>
  )
}

