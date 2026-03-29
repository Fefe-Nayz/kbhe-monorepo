// KBHE 6-Key RAW HID SignalRGB plugin
// Firmware requirements:
// - CMD_SET_LED_EFFECT (0x6F)
// - CMD_SET_LED_ALL_CHUNK (0x6A)
// - LED_EFFECT_THIRD_PARTY mode = 14
// - RAW HID endpoint on interface 1 / usage_page 0xFF00

const CMD_SET_LED_ENABLED = 0x61;
const CMD_SET_LED_EFFECT = 0x6F;
const CMD_SET_LED_ALL_CHUNK = 0x6A;

const LED_EFFECT_MATRIX_SOFTWARE = 0;
const LED_EFFECT_THIRD_PARTY = 14;

const PACKET_SIZE = 64;
const WRITE_SIZE = 65; // report id + 64-byte payload
const LED_COUNT = 64;
const LED_BYTES = LED_COUNT * 3;
const CHUNK_SIZE = 48;

// 6-key prototype logical layout in SignalRGB canvas space.
const vLedNames = ["Q", "W", "E", "A", "S", "D"];
const vLedPositions = [
  [0, 0],
  [1, 0],
  [2, 0],
  [0, 1],
  [1, 1],
  [2, 1],
];

// Maps the six logical keys to firmware LED indices.
const ledIndexMap = [0, 1, 2, 3, 4, 5];

let frame = new Array(LED_BYTES).fill(0);

export function Name() {
  return "KBHE 6-Key Prototype";
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
  return [3, 2];
}

export function DefaultPosition() {
  return [0, 0];
}

export function DefaultScale() {
  return 48.0;
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

function setMatrixSoftwareMode() {
  sendCommand(CMD_SET_LED_EFFECT, [LED_EFFECT_MATRIX_SOFTWARE]);
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
  for (let chunk = 0; chunk < 4; chunk++) {
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
  setMatrixSoftwareMode();
}
