import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import {
  IconRefresh,
  IconTerminal2,
  IconTrash,
  IconBolt
} from "@tabler/icons-react"

export default function Settings() {
  return (
    <div className="max-w-2xl mx-auto p-6 bg-card text-card-foreground border border-border rounded-xl shadow-sm">

      <div className="mb-8">
        <h2 className="text-xl font-semibold tracking-tight">Device Settings</h2>
        <p className="text-sm text-muted-foreground">Manage performance and startup modes.</p>
      </div>

      <div className="space-y-6">

        {/* Polling Rate */}
        <div className="flex items-start justify-between gap-4">
          <div className="space-y-1">
            <div className="flex items-center gap-2">
              <IconBolt className="size-4 text-muted-foreground" />
              <label className="text-sm font-medium leading-none">8000Hz Polling Rate</label>
            </div>
            <p className="text-sm text-muted-foreground leading-relaxed max-w-md">
              Enables ultra-fast polling for minimal latency. May increase CPU usage.
            </p>
          </div>
          <Switch id="polling-rate" />
        </div>

        <div className="h-px bg-border/60" />

        {/* Restart */}
        <div className="flex items-center justify-between gap-4">
          <div className="space-y-1">
            <label className="text-sm font-medium leading-none">Restart Keyboard</label>
            <p className="text-sm text-muted-foreground">The device will disconnect and reconnect.</p>
          </div>
          <Button variant="outline" onClick={() => console.log("Restart keyboard")}>
            <IconRefresh className="size-4" />
            Restart
          </Button>
        </div>

        <div className="h-px bg-border/60" />

        {/* Bootloader */}
        <div className="flex items-center justify-between gap-4">
          <div className="space-y-1">
            <label className="text-sm font-medium leading-none">Bootloader Mode</label>
            <p className="text-sm text-muted-foreground">Switch to flash mode for firmware update.</p>
          </div>
          <Button variant="outline" onClick={() => console.log("Enter bootloader")}>
            <IconTerminal2 className="size-4" />
            Enter
          </Button>
        </div>

        <div className="h-px bg-border/60" />

        {/* Factory Reset */}
        <div className="flex items-center justify-between gap-4">
          <div className="space-y-1">
            <label className="text-sm font-medium leading-none text-destructive">Factory Reset</label>
            <p className="text-sm text-muted-foreground">Erases all profiles and personal settings.</p>
          </div>
          <Button variant="destructive" onClick={() => console.log("Factory reset")}>
            <IconTrash className="size-4" />
            Reset
          </Button>
        </div>

      </div>
    </div>
  )
}