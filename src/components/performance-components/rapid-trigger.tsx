import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Switch } from "@/components/ui/switch"
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs"
import SliderVertical from "@/components/performance-components/slider-vertical"

interface RapidTriggerConfig {
  press: number
  release: number
  enabled: boolean
}

interface RapidTriggerCardProps {
  config: RapidTriggerConfig
  onChange: (config: RapidTriggerConfig) => void
}

export default function RapidTriggerCard({ config, onChange }: RapidTriggerCardProps) {
  return (
    <Card className="w-48 rounded-2xl border border-border bg-background shadow-sm shrink-0">
      <CardHeader className="pb-1 flex items-center justify-between flex-row">
        <div>
          <CardTitle className="text-sm">Rapid Trigger</CardTitle>
          <CardDescription className="text-xs">Sensitivity</CardDescription>
        </div>
        <Switch
          checked={config.enabled}
          onCheckedChange={() => onChange({ ...config, enabled: !config.enabled })}
        />
      </CardHeader>

      <CardContent className="flex flex-col items-center gap-2">

        {!config.enabled ? (
          // Désactivé
          <div className="flex flex-col items-center gap-2 w-full">
            <div className="flex items-center justify-between text-xs text-muted-foreground w-full">
              <span>Value</span>
              <span className="font-semibold text-foreground">{config.press.toFixed(1)} mm</span>
            </div>
            <div className="h-36 flex items-center justify-center">
              <SliderVertical value={config.press} onChange={() => {}} disabled />
            </div>
          </div>
        ) : (
          // Activé — tabs Basic / Advanced
          <Tabs defaultValue="basic" className="w-full">
            <TabsList className="w-full">
              <TabsTrigger value="basic" className="flex-1 text-xs">Basic</TabsTrigger>
              <TabsTrigger value="advanced" className="flex-1 text-xs">Advanced</TabsTrigger>
            </TabsList>

            {/* Basic */}
            <TabsContent value="basic">
              <div className="flex flex-col items-center gap-2 w-full">
                <div className="flex items-center justify-between text-xs text-muted-foreground w-full">
                  <span>Value</span>
                  <span className="font-semibold text-foreground">{config.press.toFixed(1)} mm</span>
                </div>
                <div className="h-28 flex items-center justify-center">
                  <SliderVertical
                    value={config.press}
                    onChange={(val) => onChange({ ...config, press: val })}
                  />
                </div>
              </div>
            </TabsContent>

            {/* Advanced */}
            <TabsContent value="advanced">
  <div className="flex flex-row gap-4 justify-center items-start pt-2">
    
    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-muted-foreground">Press</span>
      <span className="text-xs font-semibold">{config.press.toFixed(1)} mm</span>
      <div className="h-28 flex items-center justify-center">
        <SliderVertical
          value={config.press}
          onChange={(val) => onChange({ ...config, press: val })}
        />
      </div>
    </div>

    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-muted-foreground">Release</span>
      <span className="text-xs font-semibold">{config.release.toFixed(1)} mm</span>
      <div className="h-28 flex items-center justify-center">
        <SliderVertical
          value={config.release}
          onChange={(val) => onChange({ ...config, release: val })}
        />
      </div>
    </div>

  </div>
</TabsContent>
          </Tabs>
        )}

      </CardContent>
    </Card>
  )
}