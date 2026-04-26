import { invoke } from "@tauri-apps/api/core";
import { kbheDevice } from "./device";

const POLL_INTERVAL_MS = 150;
const HEARTBEAT_MS = 750;

let running = false;
let timer: ReturnType<typeof setInterval> | null = null;
let lastSentLevel: number | null = null;
let lastSentAt = 0;

async function getSystemVolume(): Promise<number | null> {
  try {
    return await invoke<number | null>("kbhe_get_system_volume");
  } catch {
    return null;
  }
}

async function tick() {
  const level = await getSystemVolume();
  if (level == null) return;

  const now = Date.now();
  const changed = level !== lastSentLevel;
  const heartbeat = now - lastSentAt > HEARTBEAT_MS;

  if (changed || heartbeat) {
    try {
      await kbheDevice.ledSetVolumeOverlay(level);
      lastSentLevel = level;
      lastSentAt = now;
    } catch {
      // device disconnected or busy - service will be stopped externally
    }
  }
}

export function startVolumeService() {
  if (running) return;
  running = true;
  lastSentLevel = null;
  lastSentAt = 0;
  timer = setInterval(() => void tick(), POLL_INTERVAL_MS);
}

export function stopVolumeService() {
  if (!running) return;
  running = false;
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
  lastSentLevel = null;
  void kbheDevice.ledClearVolumeOverlay().catch(() => {});
}

export function isVolumeServiceRunning(): boolean {
  return running;
}
