import { useState, useMemo } from "react";
import { HID_KEYCODES } from "@/lib/kbhe/protocol";
import { buildKeycodeLegendSlots } from "@/lib/kbhe/keycode-icons";
import { useOSKeycapLegend, type KeycapLegend } from "@/hooks/use-os-layout";
import { KeycapButton } from "@/components/keycap-button";
import { Input } from "@/components/ui/input";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from "@/components/ui/accordion";
import { cn } from "@/lib/utils";
import { IconSearch } from "@tabler/icons-react";

const KEYCODE_TILE_UNIT = 50;

interface KeycodeEntry {
  name: string;
  legend: KeycapLegend;
  code: number;
}

interface KeycodeCategory {
  label: string;
  keys: KeycodeEntry[];
}

const FRIENDLY_LABELS: Record<string, string> = {
  NO: "No",
  TRANSPARENT: "Transparent",
  ENTER: "Enter",
  ESC: "Esc",
  BACKSPACE: "Bksp",
  TAB: "Tab",
  SPACE: "Space",
  CAPSLOCK: "Caps",
  PRINTSCREEN: "PrtSc",
  SCROLLLOCK: "ScrLk",
  INSERT: "Ins",
  DELETE: "Del",
  PAGEUP: "PgUp",
  PAGEDOWN: "PgDn",
  APPLICATION: "Menu",
  LEFTBRACE: "[",
  RIGHTBRACE: "]",
  MINUS: "-",
  EQUAL: "=",
  GRAVE: "`",
  APOSTROPHE: "'",
  SEMICOLON: ";",
  COMMA: ",",
  DOT: ".",
  SLASH: "/",
  BACKSLASH: "\\",
  NONUS_HASH: "ISO #",
  NONUS_BACKSLASH: "ISO \\",
  LCTRL: "L-Ctrl",
  RCTRL: "R-Ctrl",
  LSHIFT: "L-Shift",
  RSHIFT: "R-Shift",
  LALT: "L-Alt",
  RALT: "R-Alt",
  LGUI: "L-Win",
  RGUI: "R-Win",
  AUDIO_MUTE: "Audio Mute",
  AUDIO_VOL_UP: "Audio Vol Up",
  AUDIO_VOL_DOWN: "Audio Vol Down",
  MEDIA_PLAY_PAUSE: "Media Play/Pause",
  MEDIA_NEXT_TRACK: "Media Next",
  MEDIA_PREV_TRACK: "Media Prev",
  MEDIA_STOP: "Media Stop",
  MEDIA_SELECT: "Media Select",
  MY_COMPUTER: "File Browser",
  WWW_SEARCH: "Browser Search",
  WWW_HOME: "Browser Home",
  WWW_BACK: "Browser Back",
  WWW_FORWARD: "Browser Forward",
  WWW_REFRESH: "Browser Refresh",
  WWW_FAVORITES: "Browser Favorites",
  BRIGHTNESS_UP: "Brightness Up",
  BRIGHTNESS_DOWN: "Brightness Down",
  CONTROL_PANEL: "Control Panel",
  KB_POWER: "System Power",
};

const KEYPAD_LABELS: Record<string, string> = {
  DIVIDE: "/",
  MULTIPLY: "*",
  MINUS: "-",
  PLUS: "+",
  ENTER: "Enter",
  DOT: ".",
  EQUAL: "=",
};

function formatKeycodeName(name: string): string {
  const known = FRIENDLY_LABELS[name];
  if (known) return known;

  if (/^KP_/.test(name)) {
    const token = name.slice(3);
    if (/^[0-9]$/.test(token)) return `Num ${token}`;
    return `Num ${KEYPAD_LABELS[token] ?? token}`;
  }

  if (name.includes(" ") || /^[A-Z]$/.test(name) || /^[0-9]$/.test(name) || /^F\d+$/.test(name)) {
    return name;
  }

  if (name.includes("_")) {
    return name
      .split("_")
      .map((part) => part.charAt(0) + part.slice(1).toLowerCase())
      .join(" ");
  }

  return name;
}

function categorize(resolveLegend: (hidKeycode: number, fallbackName: string) => KeycapLegend): KeycodeCategory[] {
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

    const entry = {
      name,
      legend: resolveLegend(code, formatKeycodeName(name)),
      code,
    };
    if (/^[A-Z]$/.test(name)) letters.push(entry);
    else if (/^[0-9]$/.test(name)) numbers.push(entry);
    else if (/^F\d+$/.test(name)) fkeys.push(entry);
    else if (/^KP_/.test(name)) numpad.push(entry);
    else if (/CTRL|SHIFT|ALT|GUI/.test(name)) modifiers.push(entry);
    else if (/MEDIA|AUDIO|VOLUME|MUTE|MAIL|CALCULATOR|WWW|BRIGHTNESS|CONTROL_PANEL/.test(name)) media.push(entry);
    else if (/^(UP|DOWN|LEFT|RIGHT|HOME|END|PAGEUP|PAGEDOWN|INSERT|DELETE)$/.test(name) && code < 0x100) navigation.push(entry);
    else if (/MOUSE/.test(name)) mouse.push(entry);
    else if (/Layer|FN|MO|TG|Clear Layer/.test(name)) layers.push(entry);
    else if (/LED/.test(name)) led.push(entry);
    else if (/GP |Gamepad/.test(name)) gamepad.push(entry);
    else if (code === 0 || code === 1) special.push(entry);
    else system.push(entry);
  }

  const categories = [
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

  for (const category of categories) {
    category.keys.sort((a, b) =>
      a.legend.searchText.localeCompare(b.legend.searchText, undefined, { sensitivity: "base" }),
    );
  }

  return categories;
}

interface KeycodeAccordionProps {
  onSelect: (code: number, name: string) => void;
  selectedCode?: number;
  className?: string;
  resolveLegend?: (hidKeycode: number, fallbackName: string) => KeycapLegend;
}

export function KeycodeAccordion({ onSelect, selectedCode, className, resolveLegend }: KeycodeAccordionProps) {
  const [search, setSearch] = useState("");
  const hookResolveKeycapLegend = useOSKeycapLegend();
  const resolveKeycapLegend = resolveLegend ?? hookResolveKeycapLegend;

  const categories = useMemo(() => categorize(resolveKeycapLegend), [resolveKeycapLegend]);

  const filtered = useMemo(() => {
    if (!search.trim()) return categories;
    const q = search.toLowerCase();
    return categories.map((cat) => ({
      ...cat,
      keys: cat.keys.filter((k) =>
        k.legend.searchText.includes(q) || k.name.toLowerCase().includes(q),
      ),
    })).filter((cat) => cat.keys.length > 0);
  }, [search, categories]);

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
                    <KeycapButton
                      key={k.code}
                      keyId={`keycode-${k.code}`}
                      legendSlots={buildKeycodeLegendSlots(k.code, k.legend.slots, "size-3.5")}
                      labelText={k.legend.text}
                      unit={KEYCODE_TILE_UNIT}
                      selected={selectedCode === k.code}
                      className={cn("rounded-md", selectedCode === k.code && "ring-2 ring-primary/20")}
                      onClick={() => onSelect(k.code, k.legend.text)}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        onSelect(0, "NO");
                      }}
                    />
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
