# react-kle-modern

Modern **TypeScript + React** parser and renderer for **Keyboard Layout Editor (KLE)** JSON.

It is designed for apps that want to:

- import a `keyboard-layout.json` from Keyboard Layout Editor
- parse it into a typed model
- render it with a more modern UI than the legacy KLE viewer
- fit visually into a **shadcn/ui + Tailwind** app

## Features

- KLE-compatible parser for the serialized row/object format
- React renderer with absolute positioning, rotation support, legends, stepped/nub/ghost/decal states
- Styling based on CSS variables compatible with common shadcn theme tokens (`--background`, `--card`, `--border`, `--ring`, etc.)
- Typed public API
- Works with raw JSON string, deserialized KLE array, or already-parsed internal model

## Install

```bash
npm install react-kle-modern
```

or

```bash
pnpm add react-kle-modern
```

## Local Setup (Repo)

If you are working on this repository locally:

1. Install dependencies at the repo root.
2. Start the example app.

### With Bun

```bash
bun install
bun run example
```

### With npm

```bash
npm install
npm run example
```

### With pnpm

```bash
pnpm install
pnpm run example
```

The example app is served by Vite (usually on http://localhost:5173).

To expose it on your local network:

```bash
bun run example:host
```

Useful repo scripts:

- `bun run build` to build the library in `dist/`
- `bun run typecheck` to run TypeScript checks

## Usage

```tsx
import { KeyboardLayout } from "react-kle-modern";
import "react-kle-modern/styles.css";

const kle = `[
  [{"name":"My Board"}],
  ["Esc",{"x":0.5},"F1","F2","F3"],
  [{"y":0.5},"Tab","Q","W","E","R"]
]`;

export default function Demo() {
  return (
    <div className="p-6">
      <KeyboardLayout layout={kle} interactive />
    </div>
  );
}
```

## Parser API

```ts
import { parseKLE, deserializeKLE } from "react-kle-modern";

const parsed = parseKLE(kleJsonString);
const parsed2 = deserializeKLE(kleArray);
```

### Returned shape

```ts
interface ParsedKeyboardLayout {
  meta: KeyboardMeta;
  keys: KeyboardKey[];
}
```

Each key contains normalized geometry:

- `x`, `y`, `width`, `height`
- `x2`, `y2`, `width2`, `height2`
- `rotationX`, `rotationY`, `rotationAngle`
- `labels`, `textColor`, `textSize`
- `color`, `profile`, `nub`, `stepped`, `ghost`, `decal`

## Renderer props

```ts
interface KeyboardLayoutProps {
  layout: string | unknown[] | ParsedKeyboardLayout;
  className?: string;
  unit?: number;               // default 56
  gap?: number;                // default 6
  showLegendSlots?: boolean;   // default false
  interactive?: boolean;       // default false
  selectedKeyId?: string;
  onKeyClick?: (key: KeyboardKey) => void;
  renderLegend?: (args: { key: KeyboardKey; label: string; index: number }) => React.ReactNode;
}
```

## Notes

- The parser targets the common KLE serialized format used by Keyboard Layout Editor.
- Rotation is supported.
- Non-rectangular keys using the secondary rectangle (`x2/y2/w2/h2`) are rendered as a two-piece keycap composition. This looks modern and works well visually, though it is intentionally not a pixel-for-pixel clone of the legacy KLE renderer.
- The default look is meant to feel close to shadcn cards/buttons rather than old-school KLE caps.

## Theming

The stylesheet reads these CSS variables when available:

- `--background`
- `--foreground`
- `--card`
- `--card-foreground`
- `--border`
- `--ring`
- `--radius`

So in a shadcn app it should inherit the general design language automatically.

## Suggested next steps

Good additions if you want to keep evolving the package:

- zoom / pan
- SVG export
- per-key hover tooltips with matrix / switch info
- draggable selection overlay
- exact stitched rendering for L-shaped keys
- editor mode with hit-testing and handles
