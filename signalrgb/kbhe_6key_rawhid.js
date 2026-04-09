// KBHE 82-Key RAW HID SignalRGB plugin
// Firmware requirements:
// - CMD_SET_LED_EFFECT (0x6F)
// - CMD_SET_LED_ALL_CHUNK (0x6A)
// - CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY (0x7B)
// - LED_EFFECT_THIRD_PARTY mode = 14
// - RAW HID endpoint on interface 1 / usage_page 0xFF00

const CMD_SET_LED_ENABLED = 0x61;
const CMD_SET_LED_EFFECT = 0x6F;
const CMD_SET_LED_ALL_CHUNK = 0x6A;
const CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY = 0x7B;

const LED_EFFECT_THIRD_PARTY = 14;

const PACKET_SIZE = 64;
const WRITE_SIZE = 65; // report id + 64-byte payload
const LED_COUNT = 82;
const LED_BYTES = LED_COUNT * 3;
const CHUNK_SIZE = 60;
const DEVICE_SIZE = [16.25, 6.5];

// Logical key order follows the firmware NUM_KEYS layout (K01..K82).
// The firmware already remaps logical indices to the physical WS2812 chain.
const KEY_LAYOUT = [
  { name: "K01 Esc", x: 0.0, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K02 F1", x: 1.25, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K03 F2", x: 2.25, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K04 F3", x: 3.25, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K05 F4", x: 4.25, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K06 F5", x: 5.5, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K07 F6", x: 6.5, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K08 F7", x: 7.5, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K09 F8", x: 8.5, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K10 F9", x: 9.75, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K11 F10", x: 10.75, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K12 F11", x: 11.75, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K13 F12", x: 12.75, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K14 Delete", x: 14.0, y: 0.0, w: 1.0, h: 1.0 },
  { name: "K15 Sup2", x: 0.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K16 Ampersand", x: 1.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K17 EAcute", x: 2.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K18 Quote", x: 3.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K19 Apostrophe", x: 4.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K20 LeftParen", x: 5.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K21 Minus", x: 6.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K22 EGrave", x: 7.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K23 Underscore", x: 8.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K24 CCedilla", x: 9.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K25 AGrave", x: 10.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K26 RightParen", x: 11.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K27 Equal", x: 12.0, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K28 Backspace", x: 13.0, y: 1.25, w: 2.0, h: 1.0 },
  { name: "K29 PgUp", x: 15.25, y: 1.25, w: 1.0, h: 1.0 },
  { name: "K30 Tab", x: 0.0, y: 2.25, w: 1.5, h: 1.0 },
  { name: "K31 A", x: 1.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K32 Z", x: 2.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K33 E", x: 3.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K34 R", x: 4.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K35 T", x: 5.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K36 Y", x: 6.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K37 U", x: 7.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K38 I", x: 8.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K39 O", x: 9.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K40 P", x: 10.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K41 Diaeresis", x: 11.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K42 Pound", x: 12.5, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K43 Enter", x: 13.75, y: 2.25, w: 1.25, h: 2.0 },
  { name: "K44 PgDn", x: 15.25, y: 2.25, w: 1.0, h: 1.0 },
  { name: "K45 CapsLock", x: 0.0, y: 3.25, w: 1.75, h: 1.0 },
  { name: "K46 Q", x: 1.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K47 S", x: 2.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K48 D", x: 3.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K49 F", x: 4.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K50 G", x: 5.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K51 H", x: 6.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K52 J", x: 7.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K53 K", x: 8.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K54 L", x: 9.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K55 M", x: 10.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K56 Percent", x: 11.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K57 Mu", x: 12.75, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K58 Home", x: 15.25, y: 3.25, w: 1.0, h: 1.0 },
  { name: "K59 LeftShift", x: 0.0, y: 4.25, w: 1.25, h: 1.0 },
  { name: "K60 GreaterThan", x: 1.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K61 W", x: 2.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K62 X", x: 3.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K63 C", x: 4.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K64 V", x: 5.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K65 B", x: 6.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K66 N", x: 7.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K67 QuestionMark", x: 8.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K68 Period", x: 9.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K69 Slash", x: 10.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K70 Section", x: 11.25, y: 4.25, w: 1.0, h: 1.0 },
  { name: "K71 RightShift", x: 12.25, y: 4.25, w: 1.75, h: 1.0 },
  { name: "K72 ArrowUp", x: 14.25, y: 4.5, w: 1.0, h: 1.0 },
  { name: "K73 LeftCtrl", x: 0.0, y: 5.25, w: 1.25, h: 1.0 },
  { name: "K74 Win", x: 1.25, y: 5.25, w: 1.25, h: 1.0 },
  { name: "K75 Alt", x: 2.5, y: 5.25, w: 1.25, h: 1.0 },
  { name: "K76 Space", x: 3.75, y: 5.25, w: 6.25, h: 1.0 },
  { name: "K77 AltGr", x: 10.0, y: 5.25, w: 1.0, h: 1.0 },
  { name: "K78 Fn", x: 11.0, y: 5.25, w: 1.0, h: 1.0 },
  { name: "K79 RightCtrl", x: 12.0, y: 5.25, w: 1.0, h: 1.0 },
  { name: "K80 ArrowLeft", x: 13.25, y: 5.5, w: 1.0, h: 1.0 },
  { name: "K81 ArrowDown", x: 14.25, y: 5.5, w: 1.0, h: 1.0 },
  { name: "K82 ArrowRight", x: 15.25, y: 5.5, w: 1.0, h: 1.0 },
];

const vLedNames = KEY_LAYOUT.map((key) => key.name);
const vLedPositions = KEY_LAYOUT.map((key) => [
  key.x + key.w / 2,
  key.y + key.h / 2,
]);
const ledIndexMap = KEY_LAYOUT.map((_key, index) => index);

let frame = new Array(LED_BYTES).fill(0);

export function Name() {
  return "KBHE 82-Key";
}

export function Publisher() {
  return "Fefe-Nayz";
}

export function VendorId() {
  return 0x9172;
}

export function ProductId() {
  return 0x0002;
}

export function Type() {
  return "hid";
}

export function Size() {
  return DEVICE_SIZE;
}

export function DefaultPosition() {
  return [0, 0];
}

export function DefaultScale() {
  return 18.0;
}

export function LedNames() {
  return vLedNames;
}

export function LedPositions() {
  return vLedPositions;
}

export function Validate(endpoint) {
  return endpoint.interface === 1 && endpoint.usage_page === 0xff00;
}

function sendCommand(commandId, payload) {
  const packet = new Array(PACKET_SIZE).fill(0);
  packet[0] = commandId & 0xff;
  packet[1] = 0x00; // status/padding byte for request packets

  for (let i = 0; i < payload.length && i < 62; i++) {
    packet[2 + i] = payload[i] & 0xff;
  }

  // Most HID devices used by SignalRGB need a leading report-id byte.
  const writePacket = [0x00].concat(packet);
  device.write(writePacket, WRITE_SIZE);
}

function setThirdPartyMode() {
  sendCommand(CMD_SET_LED_ENABLED, [0x01]);
  sendCommand(CMD_SET_LED_EFFECT, [LED_EFFECT_THIRD_PARTY]);
}

function restoreEffectBeforeThirdParty() {
  sendCommand(CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY, []);
}

function collectFrameFromCanvas() {
  frame.fill(0);

  for (let i = 0; i < ledIndexMap.length; i++) {
    const ledIndex = ledIndexMap[i];
    const [x, y] = vLedPositions[i];
    const color = device.color(x, y);
    const base = ledIndex * 3;

    frame[base + 0] = color[0] & 0xff;
    frame[base + 1] = color[1] & 0xff;
    frame[base + 2] = color[2] & 0xff;
  }
}

function pushFrame() {
  const chunkCount = Math.ceil(LED_BYTES / CHUNK_SIZE);

  for (let chunk = 0; chunk < chunkCount; chunk++) {
    const offset = chunk * CHUNK_SIZE;
    const remaining = LED_BYTES - offset;
    const size = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    if (size <= 0) {
      break;
    }

    const payload = [chunk, size].concat(frame.slice(offset, offset + size));
    sendCommand(CMD_SET_LED_ALL_CHUNK, payload);
  }
}

export function Initialize() {
  setThirdPartyMode();
  device.clearReadBuffer();
}

export function Render() {
  collectFrameFromCanvas();
  pushFrame();
  device.pause(1);
}

export function Shutdown(_SystemSuspending) {
  restoreEffectBeforeThirdParty();
}
