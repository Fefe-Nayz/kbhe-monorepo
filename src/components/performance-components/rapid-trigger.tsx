import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Slider } from "@/components/ui/slider"
import { Switch } from "@/components/ui/switch"

interface RapidTriggerCardProps {
  value: number
  enabled: boolean
  onToggle: () => void
  onChange: (value: number) => void
}

export default function RapidTriggerCard({ value, enabled, onToggle, onChange }: RapidTriggerCardProps) {
  return (
    <Card className="w-125 rounded-3xl border border-border bg-background shadow-sm">
      <CardHeader className="flex items-center justify-between">
        <div>
          <CardTitle>Rapid Trigger</CardTitle>
          <CardDescription>When toggled, adjust the sensitivity and enable or disable rapid mode.</CardDescription>
        </div>
        <Switch checked={enabled} onCheckedChange={onToggle} />
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex items-center justify-between text-sm text-muted-foreground">
          <span>Value</span>
          <span className="font-semibold text-foreground">{value.toFixed(1)}</span>
        </div>
        <Slider
          value={[value]}
          min={0.2}
          max={3}
          step={0.1}
          disabled={!enabled}
          onValueChange={(newValue) => {
            const next = Array.isArray(newValue) ? newValue[0] : newValue
            onChange(Number(next.toFixed(1)))
          }}
        />
      </CardContent>
    </Card>
  )
}
