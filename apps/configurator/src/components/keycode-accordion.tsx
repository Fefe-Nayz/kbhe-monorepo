import { useState, useMemo } from "react";
import { HID_KEYCODES } from "@/lib/kbhe/protocol";
import { buildKeycodeLegendSlots } from "@/lib/kbhe/keycode-icons";
import { useOSKeycapLegend, type KeycapLegend } from "@/hooks/use-os-layout";
import { KeycapButton } from "@/components/keycap-button";
import { Input } from "@/components/ui/input";
import { EmptyState } from "@/components/shared/EmptyState";
import { cn } from "@/lib/utils";
import { IconSearch, IconX, IconMoodEmpty } from "@tabler/icons-react";

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
  const profiles: KeycodeCategory["keys"] = [];
  const macros: KeycodeCategory["keys"] = [];
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
    else if (/^Profile /.test(name)) profiles.push(entry);
    else if (/^Macro \d+$/.test(name)) macros.push(entry);
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
    { label: "Profiles", keys: profiles },
    { label: "Macros", keys: macros },
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
  selectedCodes?: number[];
  className?: string;
  resolveLegend?: (hidKeycode: number, fallbackName: string) => KeycapLegend;
  /** Message shown above the picker when nothing on the keyboard is selected. */
  hint?: string;
}

const ALL_CATEGORIES = "__all__";

/**
 * Keycode picker. Every category is visible at once under sticky headers —
 * the previous accordion hid 14 of 15 groups behind a click, which made
 * assigning anything but a letter a scavenger hunt.
 */
export function KeycodeAccordion({
  onSelect,
  selectedCode,
  selectedCodes,
  className,
  resolveLegend,
  hint,
}: KeycodeAccordionProps) {
  const [search, setSearch] = useState("");
  const [activeCategory, setActiveCategory] = useState<string>(ALL_CATEGORIES);
  const hookResolveKeycapLegend = useOSKeycapLegend();
  const resolveKeycapLegend = resolveLegend ?? hookResolveKeycapLegend;

  const selectedCodesSet = useMemo(() => {
    const merged = new Set<number>();
    if (typeof selectedCode === "number") {
      merged.add(selectedCode);
    }
    for (const code of selectedCodes ?? []) {
      merged.add(code);
    }
    return merged;
  }, [selectedCode, selectedCodes]);

  const categories = useMemo(() => categorize(resolveKeycapLegend), [resolveKeycapLegend]);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return categories
      .filter((cat) => activeCategory === ALL_CATEGORIES || cat.label === activeCategory)
      .map((cat) =>
        q
          ? {
              ...cat,
              keys: cat.keys.filter(
                (k) => k.legend.searchText.includes(q) || k.name.toLowerCase().includes(q),
              ),
            }
          : cat,
      )
      .filter((cat) => cat.keys.length > 0);
  }, [search, categories, activeCategory]);

  const totalMatches = filtered.reduce((sum, cat) => sum + cat.keys.length, 0);

  return (
    <div className={cn("flex flex-col", className)}>
      {hint && (
        <div className="sticky top-0 z-30 -mx-5 bg-background px-5 pb-3 pt-0.5">
          <div className="flex items-center gap-2 rounded-lg border border-primary/25 bg-primary/8 px-3 py-2 text-xs text-primary">
            <IconSearch className="size-3.5 shrink-0" />
            <span>{hint}</span>
          </div>
        </div>
      )}

      <div
        className={cn(
          "sticky z-20 -mx-5 flex flex-col gap-2.5 border-b bg-background/95 px-5 pb-3 backdrop-blur-sm",
          hint ? "top-12 pt-0" : "-mt-5 top-0 pt-5",
        )}
      >
        <div className="relative">
          <IconSearch className="pointer-events-none absolute left-3 top-1/2 size-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            placeholder="Search keycodes…"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="h-9 pl-9 pr-9 text-sm"
          />
          {search && (
            <button
              type="button"
              aria-label="Clear search"
              onClick={() => setSearch("")}
              className="absolute right-2 top-1/2 flex size-5 -translate-y-1/2 items-center justify-center rounded text-muted-foreground transition-colors hover:bg-muted hover:text-foreground"
            >
              <IconX className="size-3.5" />
            </button>
          )}
        </div>

        <div className="-mx-1 flex flex-wrap gap-1.5 px-1">
          <CategoryChip
            label="All"
            count={categories.reduce((sum, cat) => sum + cat.keys.length, 0)}
            active={activeCategory === ALL_CATEGORIES}
            onClick={() => setActiveCategory(ALL_CATEGORIES)}
          />
          {categories.map((cat) => (
            <CategoryChip
              key={cat.label}
              label={cat.label}
              count={cat.keys.length}
              active={activeCategory === cat.label}
              onClick={() =>
                setActiveCategory((current) =>
                  current === cat.label ? ALL_CATEGORIES : cat.label,
                )
              }
            />
          ))}
        </div>
      </div>

      <div className="mt-4">
        {totalMatches === 0 ? (
          <EmptyState
            icon={<IconMoodEmpty />}
            title="No matching keycodes"
            description={`Nothing matches “${search}”. Try a shorter query or pick another category.`}
            size="sm"
          />
        ) : (
          <div className="flex flex-col gap-4 pb-2">
            {filtered.map((cat) => (
              <section key={cat.label}>
                <div
                  className={cn(
                    "sticky z-10 -mx-1 mb-2 flex items-center gap-2 bg-background/95 px-1 py-1.5 backdrop-blur-sm",
                    hint ? "top-[10.25rem]" : "top-[8.5rem]",
                  )}
                >
                  <h4 className="text-[0.7rem] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
                    {cat.label}
                  </h4>
                  <span className="rounded-full bg-muted px-1.5 py-px text-[0.65rem] font-medium tabular-nums text-muted-foreground">
                    {cat.keys.length}
                  </span>
                  <div className="h-px flex-1 bg-border" />
                </div>
                <div className="flex flex-wrap gap-1.5">
                  {cat.keys.map((k) => {
                    const isSelected = selectedCodesSet.has(k.code);
                    return (
                      <KeycapButton
                        key={k.code}
                        keyId={`keycode-${k.code}`}
                        legendSlots={buildKeycodeLegendSlots(k.code, k.legend.slots, "size-3.5")}
                        labelText={k.legend.text}
                        unit={KEYCODE_TILE_UNIT}
                        selected={isSelected}
                        className={cn("rounded-md", isSelected && "ring-2 ring-primary/40")}
                        onClick={() => onSelect(k.code, k.legend.text)}
                        onContextMenu={(e) => {
                          e.preventDefault();
                          onSelect(0, "NO");
                        }}
                      />
                    );
                  })}
                </div>
              </section>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

function CategoryChip({
  label,
  count,
  active,
  onClick,
}: {
  label: string;
  count: number;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      aria-pressed={active}
      className={cn(
        "inline-flex h-6.5 items-center gap-1.5 rounded-full border px-2.5 text-xs font-medium transition-colors",
        "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60",
        active
          ? "border-primary/40 bg-primary/12 text-primary"
          : "border-border bg-card text-muted-foreground hover:bg-muted hover:text-foreground",
      )}
    >
      {label}
      <span
        className={cn(
          "tabular-nums",
          active ? "text-primary/70" : "text-muted-foreground/60",
        )}
      >
        {count}
      </span>
    </button>
  );
}
