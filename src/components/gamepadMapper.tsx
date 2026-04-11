import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs"
import { Switch } from "@/components/ui/switch"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Label } from "@/components/ui/label"
import { Separator } from "@/components/ui/separator"
import { useState } from "react"

const gamepadButtons = [
  { id: "a", label: "A" },
  { id: "b", label: "B" },
  { id: "x", label: "X" },
  { id: "y", label: "Y" },
  { id: "lb", label: "LB" },
  { id: "rb", label: "RB" },
  { id: "lt", label: "LT" },
  { id: "rt", label: "RT" },
  { id: "start", label: "Start" },
  { id: "select", label: "Select" },
  { id: "l3", label: "L3" },
  { id: "r3", label: "R3" },
  { id: "dpad_up", label: "D-Up" },
  { id: "dpad_down", label: "D-Down" },
  { id: "dpad_left", label: "D-Left" },
  { id: "dpad_right", label: "D-Right" },
  { id: "ls_up", label: "LS ↑" },
  { id: "ls_down", label: "LS ↓" },
  { id: "ls_left", label: "LS ←" },
  { id: "ls_right", label: "LS →" },
  { id: "rs_up", label: "RS ↑" },
  { id: "rs_down", label: "RS ↓" },
  { id: "rs_left", label: "RS ←" },
  { id: "rs_right", label: "RS →" },
]

interface GamepadProps {
  xinputEnabled?: boolean
}

export default function Gamepad({ xinputEnabled = false }: GamepadProps) {
  const [selected, setSelected] = useState<string | null>(null)
  const [keyboardInputs, setKeyboardInputs] = useState(false)
  const [gamepadOverride, setGamepadOverride] = useState(false)

  return (
    <div className="p-6">
      <Tabs defaultValue="setup">

        {/* Header */}
        <div className="flex items-center gap-3 mb-4">
          <TabsList>
            <TabsTrigger value="setup">Setup</TabsTrigger>
            <TabsTrigger value="analog">Analog</TabsTrigger>
          </TabsList>
          <Badge variant={xinputEnabled ? "default" : "destructive"}>
            {xinputEnabled ? "XInput interface is enabled" : "XInput interface is disabled"}
          </Badge>
        </div>

        {/* Setup */}
        <TabsContent value="setup">
          <div className="flex gap-8">

            {/* Left — buttons */}
            <div className="space-y-3">
              <div>
                <p className="text-sm font-medium">Configure Controller Bindings</p>
                <p className="text-sm text-muted-foreground">Assign gamepad buttons to your keyboard.</p>
              </div>
              <div className="flex flex-wrap gap-2 max-w-xl">
                {gamepadButtons.map((btn) => (
                  <Button
                    key={btn.id}
                    variant={selected === btn.id ? "default" : "outline"}
                    size="sm"
                    onClick={() => setSelected(btn.id === selected ? null : btn.id)}
                  >
                    {btn.label}
                  </Button>
                ))}
              </div>
            </div>

            <Separator orientation="vertical" className="h-auto" />

            {/* Right — switches */}
            <div className="space-y-4 min-w-64">

              <div className="flex items-start gap-3">
                <Switch
                  id="keyboard-inputs"
                  checked={keyboardInputs}
                  onCheckedChange={setKeyboardInputs}
                />
                <div className="space-y-1">
                  <Label htmlFor="keyboard-inputs">Enable Keyboard Inputs</Label>
                  <p className="text-sm text-muted-foreground">
                    Allow keyboard inputs to be sent along with gamepad inputs.
                  </p>
                </div>
              </div>

              <div className="flex items-start gap-3">
                <Switch
                  id="gamepad-override"
                  checked={gamepadOverride}
                  onCheckedChange={setGamepadOverride}
                  disabled={!keyboardInputs}
                />
                <div className="space-y-1">
                  <Label
                    htmlFor="gamepad-override"
                    className={!keyboardInputs ? "text-muted-foreground" : ""}
                  >
                    Gamepad Override
                  </Label>
                  <p className="text-sm text-muted-foreground">
                    Disable keyboard inputs on keys bound to gamepad buttons.
                  </p>
                </div>
              </div>

            </div>
          </div>
        </TabsContent>

        {/* Analog */}
        <TabsContent value="analog">
          <p className="text-sm text-muted-foreground">Analog configuration coming soon.</p>
        </TabsContent>

      </Tabs>
    </div>
  )
}