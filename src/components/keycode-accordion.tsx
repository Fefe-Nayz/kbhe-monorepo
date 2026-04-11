import { useState, useMemo } from "react";
import { HID_KEYCODES } from "@/lib/kbhe/protocol";
import { Input } from "@/components/ui/input";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Button } from "@/components/ui/button";
import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from "@/components/ui/accordion";
import { cn } from "@/lib/utils";
import { IconSearch } from "@tabler/icons-react";

interface KeycodeCategory {
  label: string;
  keys: { name: string; code: number }[];
}

function categorize(): KeycodeCategory[] {
  const letters: KeycodeCategory["keys"] = [];
  const numbers: KeycodeCategory["keys"] = [];
  const modifiers: KeycodeCategory["keys"] = [];
  const navigation: KeycodeCategory["keys"] = [];
  const fkeys: KeycodeCategory["keys"] = [];
  const media: KeycodeCategory["keys"] = [];
  const mouse: KeycodeCategory["keys"] = [];
  const numpad: KeycodeCategory["keys"] = [];
  const layers: KeycodeCategory["keys"] = [];
  const led: KeycodeCategory["keys"] = [];
  const gamepad: KeycodeCategory["keys"] = [];
  const system: KeycodeCategory["keys"] = [];
  const special: KeycodeCategory["keys"] = [];

  const seen = new Set<number>();
  for (const [name, code] of Object.entries(HID_KEYCODES)) {
    if (name === "FN" && code === 0xf000) continue;
    if (seen.has(code)) continue;
    seen.add(code);

    const entry = { name, code };
    if (/^[A-Z]$/.test(name)) letters.push(entry);
    else if (/^[0-9]$/.test(name)) numbers.push(entry);
    else if (/^F\d+$/.test(name)) fkeys.push(entry);
    else if (/^KP_/.test(name)) numpad.push(entry);
    else if (/CTRL|SHIFT|ALT|GUI/.test(name)) modifiers.push(entry);
    else if (/UP|DOWN|LEFT|RIGHT|HOME|END|PAGE|INSERT|DELETE/.test(name) && code < 0x100) navigation.push(entry);
    else if (/MEDIA|AUDIO|VOLUME|MUTE|MAIL|CALCULATOR|WWW|BRIGHTNESS|CONTROL_PANEL/.test(name)) media.push(entry);
    else if (/MOUSE/.test(name)) mouse.push(entry);
    else if (/Layer|FN|MO|TG|Clear Layer/.test(name)) layers.push(entry);
    else if (/LED/.test(name)) led.push(entry);
    else if (/GP |Gamepad/.test(name)) gamepad.push(entry);
    else if (code === 0 || code === 1) special.push(entry);
    else system.push(entry);
  }

  return [
    { label: "Special", keys: special },
    { label: "Letters", keys: letters },
    { label: "Numbers", keys: numbers },
    { label: "Modifiers", keys: modifiers },
    { label: "Navigation", keys: navigation },
    { label: "Function Keys", keys: fkeys },
    { label: "Numpad", keys: numpad },
    { label: "Media", keys: media },
    { label: "Mouse", keys: mouse },
    { label: "Layers", keys: layers },
    { label: "LED Control", keys: led },
    { label: "Gamepad", keys: gamepad },
    { label: "System", keys: system },
  ].filter((c) => c.keys.length > 0);
}

const CATEGORIES = categorize();

interface KeycodeAccordionProps {
  onSelect: (code: number, name: string) => void;
  selectedCode?: number;
  className?: string;
}

export function KeycodeAccordion({ onSelect, selectedCode, className }: KeycodeAccordionProps) {
  const [search, setSearch] = useState("");

  const filtered = useMemo(() => {
    if (!search.trim()) return CATEGORIES;
    const q = search.toLowerCase();
    return CATEGORIES.map((cat) => ({
      ...cat,
      keys: cat.keys.filter((k) => k.name.toLowerCase().includes(q)),
    })).filter((cat) => cat.keys.length > 0);
  }, [search]);

  const defaultOpen = search.trim() ? filtered.map((c) => c.label) : ["Letters"];

  return (
    <div className={cn("flex flex-col gap-2", className)}>
      <div className="relative">
        <IconSearch className="absolute left-3 top-1/2 -translate-y-1/2 size-4 text-muted-foreground" />
        <Input
          placeholder="Search keycodes..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          className="pl-9 h-8 text-sm"
        />
      </div>
      <ScrollArea className="flex-1">
        <Accordion multiple defaultValue={defaultOpen} className="w-full">
          {filtered.map((cat) => (
            <AccordionItem key={cat.label} value={cat.label}>
              <AccordionTrigger className="text-sm font-medium py-2">
                {cat.label}
                <span className="ml-auto mr-2 text-xs text-muted-foreground">{cat.keys.length}</span>
              </AccordionTrigger>
              <AccordionContent>
                <div className="flex flex-wrap gap-1.5 pb-2">
                  {cat.keys.map((k) => (
                    <Button
                      key={k.name}
                      variant={selectedCode === k.code ? "default" : "outline"}
                      size="sm"
                      className="h-7 px-2 text-xs font-mono"
                      onClick={() => onSelect(k.code, k.name)}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        onSelect(0, "NO");
                      }}
                    >
                      {k.name}
                    </Button>
                  ))}
                </div>
              </AccordionContent>
            </AccordionItem>
          ))}
        </Accordion>
      </ScrollArea>
    </div>
  );
}
