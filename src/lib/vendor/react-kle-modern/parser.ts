import JSON5 from "json5";
import type {
  KLESerializedLayout,
  KeyboardMeta,
  KeyboardKey,
  ParsedKeyboardLayout,
} from "./types";

const LABEL_MAP: number[][] = [
  [0, 6, 2, 8, 9, 11, 3, 5, 1, 4, 7, 10],
  [1, 7, -1, -1, 9, 11, 4, -1, -1, -1, -1, 10],
  [3, -1, 5, -1, 9, 11, -1, -1, 4, -1, -1, 10],
  [4, -1, -1, -1, 9, 11, -1, -1, -1, -1, -1, 10],
  [0, 6, 2, 8, 10, -1, 3, 5, 1, 4, 7, -1],
  [1, 7, -1, -1, 10, -1, 4, -1, -1, -1, -1, -1],
  [3, -1, 5, -1, 10, -1, -1, -1, 4, -1, -1, -1],
  [4, -1, -1, -1, 10, -1, -1, -1, -1, -1, -1, -1],
];

interface WorkingKey {
  color: string;
  labels: string[];
  textColor: Array<string | undefined>;
  textSize: Array<number | undefined>;
  default: { textColor: string; textSize: number };
  x: number;
  y: number;
  width: number;
  height: number;
  x2: number;
  y2: number;
  width2: number;
  height2: number;
  rotation_x: number;
  rotation_y: number;
  rotation_angle: number;
  decal: boolean;
  ghost: boolean;
  stepped: boolean;
  nub: boolean;
  profile: string;
  sm: string;
  sb: string;
  st: string;
}

const DEFAULT_META: KeyboardMeta = {
  author: "",
  backcolor: "#eeeeee",
  background: null,
  name: "",
  notes: "",
  radii: "",
  switchBrand: "",
  switchMount: "",
  switchType: "",
};

function createWorkingKey(): WorkingKey {
  return {
    color: "#cccccc",
    labels: [],
    textColor: [],
    textSize: [],
    default: {
      textColor: "#000000",
      textSize: 3,
    },
    x: 0,
    y: 0,
    width: 1,
    height: 1,
    x2: 0,
    y2: 0,
    width2: 1,
    height2: 1,
    rotation_x: 0,
    rotation_y: 0,
    rotation_angle: 0,
    decal: false,
    ghost: false,
    stepped: false,
    nub: false,
    profile: "",
    sm: "",
    sb: "",
    st: "",
  };
}

function cloneWorkingKey(key: WorkingKey): WorkingKey {
  return {
    ...key,
    labels: [...key.labels],
    textColor: [...key.textColor],
    textSize: [...key.textSize],
    default: { ...key.default },
  };
}

function reorderLabelsIn<T>(labels: T[], align: number): T[] {
  const map = LABEL_MAP[align] ?? LABEL_MAP[4];
  const result: T[] = [];

  for (let i = 0; i < labels.length; i += 1) {
    if (labels[i] == null || labels[i] === "") continue;
    const target = map[i];
    if (target >= 0) result[target] = labels[i];
  }

  return result;
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function deserializeError(message: string, payload?: unknown): never {
  const suffix = payload === undefined ? "" : `\n${JSON5.stringify(payload)}`;
  throw new Error(`${message}${suffix}`);
}

function normalizeKey(key: WorkingKey, labels: string[], align: number, index: number): KeyboardKey {
  const next = cloneWorkingKey(key);
  next.width2 = next.width2 === 0 ? key.width : next.width2;
  next.height2 = next.height2 === 0 ? key.height : next.height2;
  next.labels = reorderLabelsIn(labels, align);
  next.textSize = reorderLabelsIn(next.textSize, align);

  for (let i = 0; i < 12; i += 1) {
    if (!next.labels[i]) {
      delete next.textSize[i];
      delete next.textColor[i];
    }
    if (next.textSize[i] === next.default.textSize) delete next.textSize[i];
    if (next.textColor[i] === next.default.textColor) delete next.textColor[i];
  }

  return {
    id: `key-${index}`,
    x: next.x,
    y: next.y,
    width: next.width,
    height: next.height,
    x2: next.x2,
    y2: next.y2,
    width2: next.width2,
    height2: next.height2,
    rotationX: next.rotation_x,
    rotationY: next.rotation_y,
    rotationAngle: next.rotation_angle,
    color: next.color,
    labels: next.labels,
    textColor: [...next.textColor],
    textSize: [...next.textSize],
    defaultLegendColor: next.default.textColor,
    defaultLegendSize: next.default.textSize,
    profile: next.profile,
    nub: next.nub,
    stepped: next.stepped,
    decal: next.decal,
    ghost: next.ghost,
    sm: next.sm,
    sb: next.sb,
    st: next.st,
  };
}

export function deserializeKLE(rows: KLESerializedLayout): ParsedKeyboardLayout {
  if (!Array.isArray(rows)) {
    deserializeError("Expected KLE layout to be an array");
  }

  const keyboard: ParsedKeyboardLayout = {
    meta: { ...DEFAULT_META },
    keys: [],
  };

  let current = createWorkingKey();
  let align = 4;
  let keyIndex = 0;

  rows.forEach((row, rowIndex) => {
    if (Array.isArray(row)) {
      row.forEach((item, itemIndex) => {
        if (typeof item === "string") {
          keyboard.keys.push(normalizeKey(current, item.split("\n"), align, keyIndex));
          keyIndex += 1;

          current.x += current.width;
          current.width = 1;
          current.height = 1;
          current.x2 = 0;
          current.y2 = 0;
          current.width2 = 0;
          current.height2 = 0;
          current.nub = false;
          current.stepped = false;
          current.decal = false;
          return;
        }

        if (!isObject(item)) {
          deserializeError("Unexpected item in KLE row", item);
        }

        if (itemIndex !== 0 && (item.r != null || item.rx != null || item.ry != null)) {
          deserializeError("Rotation can only be specified on the first key in a row", item);
        }

        if (typeof item.r === "number") current.rotation_angle = item.r;
        if (typeof item.rx === "number") current.rotation_x = item.rx;
        if (typeof item.ry === "number") current.rotation_y = item.ry;
        if (typeof item.a === "number") align = item.a;

        if (typeof item.f === "number") {
          current.default.textSize = item.f;
          current.textSize = [];
        }

        if (typeof item.f2 === "number") {
          for (let i = 1; i < 12; i += 1) current.textSize[i] = item.f2;
        }

        if (Array.isArray(item.fa)) {
          current.textSize = [...item.fa] as Array<number | undefined>;
        }

        if (typeof item.p === "string") current.profile = item.p;
        if (typeof item.c === "string") current.color = item.c;

        if (typeof item.t === "string") {
          const split = item.t.split("\n");
          if (split[0] !== "") current.default.textColor = split[0] ?? current.default.textColor;
          current.textColor = reorderLabelsIn(split, align) as Array<string | undefined>;
        }

        if (typeof item.x === "number") current.x += item.x;
        if (typeof item.y === "number") current.y += item.y;
        if (typeof item.w === "number") current.width = current.width2 = item.w;
        if (typeof item.h === "number") current.height = current.height2 = item.h;
        if (typeof item.x2 === "number") current.x2 = item.x2;
        if (typeof item.y2 === "number") current.y2 = item.y2;
        if (typeof item.w2 === "number") current.width2 = item.w2;
        if (typeof item.h2 === "number") current.height2 = item.h2;
        if (typeof item.n === "boolean") current.nub = item.n;
        if (typeof item.l === "boolean") current.stepped = item.l;
        if (typeof item.d === "boolean") current.decal = item.d;
        if (typeof item.g === "boolean") current.ghost = item.g;
        if (typeof item.sm === "string") current.sm = item.sm;
        if (typeof item.sb === "string") current.sb = item.sb;
        if (typeof item.st === "string") current.st = item.st;
      });

      current.y += 1;
      current.x = current.rotation_x;
      return;
    }

    if (isObject(row)) {
      if (rowIndex !== 0) {
        deserializeError("Keyboard metadata must be the first element", row);
      }

      keyboard.meta = {
        ...keyboard.meta,
        ...Object.fromEntries(
          Object.keys(DEFAULT_META)
            .filter((key) => row[key] != null)
            .map((key) => [key, row[key]]),
        ),
      } as KeyboardMeta;
      return;
    }

    deserializeError("Unexpected top-level KLE item", row);
  });

  return keyboard;
}

export function parseKLE(input: string): ParsedKeyboardLayout {
  const parsed = JSON5.parse(input) as KLESerializedLayout;
  return deserializeKLE(parsed);
}

export function isParsedKeyboardLayout(value: unknown): value is ParsedKeyboardLayout {
  if (!isObject(value)) return false;
  const candidate = value as { keys?: unknown; meta?: unknown };
  return Array.isArray(candidate.keys) && isObject(candidate.meta);
}

export function ensureParsedKeyboardLayout(
  input: string | KLESerializedLayout | ParsedKeyboardLayout,
): ParsedKeyboardLayout {
  if (typeof input === "string") return parseKLE(input);
  if (isParsedKeyboardLayout(input)) return input;
  return deserializeKLE(input);
}
