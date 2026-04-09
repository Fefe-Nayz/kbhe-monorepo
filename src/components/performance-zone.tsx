import { useState } from "react"

import { ScrollArea } from "@/components/ui/scroll-area"
import ActivationPointCard from "@/components/performance-components/activation-point"
import RapidTriggerCard from "@/components/performance-components/rapid-trigger"

export default function PerformanceZone() {
  const [activationValue, setActivationValue] = useState(1.2)
  const [rapidTriggerValue, setRapidTriggerValue] = useState(0.4)
  const [rapidEnabled, setRapidEnabled] = useState(false)

  return (
      <ScrollArea className="h-fit w-max rounded-3xl border border-border bg-background p-4">
        <div className="inline-flex gap-4">
          <ActivationPointCard
            value={activationValue}
            onChange={setActivationValue}
          />
          <RapidTriggerCard
            value={rapidTriggerValue}
            enabled={rapidEnabled}
            onToggle={() => setRapidEnabled((current) => !current)}
            onChange={setRapidTriggerValue}
          />
        </div>
      </ScrollArea>

  )
}
