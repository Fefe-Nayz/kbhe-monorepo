import { useState } from "react"
import { ScrollArea } from "@/components/ui/scroll-area"
import ActivationPointCard from "@/components/performance-components/activation-point"
import RapidTriggerCard from "@/components/performance-components/rapid-trigger"

export default function PerformanceZone() {
  const [activationValue, setActivationValue] = useState(1.2)
  const [rapidConfig, setRapidConfig] = useState({
    press: 0.4,
    release: 0.4,
    enabled: false
  })

  return (
    <ScrollArea className="h-70 rounded-3xl border border-border bg-background p-4">
      <div className="flex gap-4">
        <ActivationPointCard
          value={activationValue}
          onChange={setActivationValue}
        />
        <RapidTriggerCard
          config={rapidConfig}
          onChange={setRapidConfig}
        />
      </div>
    </ScrollArea>
  )
}