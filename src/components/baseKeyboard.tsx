import { IconBrandWindows } from "@tabler/icons-react"

import Key from "./keyboard-components/key"
import KeyEnter from "./keyboard-components/keyEnter"

const keyboard75Layout = [
  // Row 1
  [
    { id: "esc", label: "Esc", value: "Escape", width: 1, gap: 0 },
    { id: "f1", label: "F1", value: "F1", width: 1, gap: 0 },
    { id: "f2", label: "F2", value: "F2", width: 1, gap: 0 },
    { id: "f3", label: "F3", value: "F3", width: 1, gap: 0 },
    { id: "f4", label: "F4", value: "F4", width: 1, gap: 0 },
    { id: "f5", label: "F5", value: "F5", width: 1, gap: 0 },
    { id: "f6", label: "F6", value: "F6", width: 1, gap: 0 },
    { id: "f7", label: "F7", value: "F7", width: 1, gap: 0 },
    { id: "f8", label: "F8", value: "F8", width: 1, gap: 0 },
    { id: "f9", label: "F9", value: "F9", width: 1, gap: 0 },
    { id: "f10", label: "F10", value: "F10", width: 1, gap: 0 },
    { id: "f11", label: "F11", value: "F11", width: 1, gap: 0 },
    { id: "f12", label: "F12", value: "F12", width: 1, gap: 0 },
    { id: "del", label: "Del", value: "Delete", width: 1, gap: 0 },
  ],

  // Row 2
  [
    { id: "grave", label: "²", value: "Backquote", width: 1, gap: 0 },
    { id: "1", label: "&", value: "Digit1", width: 1, gap: 0 },
    { id: "2", label: "é", value: "Digit2", width: 1, gap: 0 },
    { id: "3", label: "\"", value: "Digit3", width: 1, gap: 0 },
    { id: "4", label: "'", value: "Digit4", width: 1, gap: 0 },
    { id: "5", label: "(", value: "Digit5", width: 1, gap: 0 },
    { id: "6", label: "-", value: "Digit6", width: 1, gap: 0 },
    { id: "7", label: "è", value: "Digit7", width: 1, gap: 0 },
    { id: "8", label: "_", value: "Digit8", width: 1, gap: 0 },
    { id: "9", label: "ç", value: "Digit9", width: 1, gap: 0 },
    { id: "0", label: "à", value: "Digit0", width: 1, gap: 0 },
    { id: "minus", label: ")", value: "Minus", width: 1, gap: 0 },
    { id: "equal", label: "=", value: "Equal", width: 1, gap: 0 },
    { id: "backspace", label: "Backspace", value: "Backspace", width: 2, gap: 0 },
    { id: "pgup", label: "PgUp", value: "PageUp", width: 1, gap: 14 },
  ],

  // Row 3
  [
    { id: "tab", label: "Tab", value: "Tab", width: 1.5, gap: 0 },
    { id: "a", label: "A", value: "KeyA", width: 1, gap: 0 },
    { id: "z", label: "Z", value: "KeyZ", width: 1, gap: 0 },
    { id: "e", label: "E", value: "KeyE", width: 1, gap: 0 },
    { id: "r", label: "R", value: "KeyR", width: 1, gap: 0 },
    { id: "t", label: "T", value: "KeyT", width: 1, gap: 0 },
    { id: "y", label: "Y", value: "KeyY", width: 1, gap: 0 },
    { id: "u", label: "U", value: "KeyU", width: 1, gap: 0 },
    { id: "i", label: "I", value: "KeyI", width: 1, gap: 0 },
    { id: "o", label: "O", value: "KeyO", width: 1, gap: 0 },
    { id: "p", label: "P", value: "KeyP", width: 1, gap: 0 },
    { id: "lbracket", label: "^", value: "BracketLeft", width: 1, gap: 0 },
    { id: "rbracket", label: "$", value: "BracketRight", width: 1, gap: 0 },
    { id: "enter", label: "Enter", value: "Enter", width: 1.3, gap: 0 },
    { id: "pgdn", label: "PgDn", value: "PageDown", width: 1, gap: 32 },
  ],

  // Row 4
  [
    { id: "capslock", label: "Caps", value: "CapsLock", width: 1.75, gap: 0 },
    { id: "q", label: "Q", value: "KeyQ", width: 1, gap: 0 },
    { id: "s", label: "S", value: "KeyS", width: 1, gap: 0 },
    { id: "d", label: "D", value: "KeyD", width: 1, gap: 0 },
    { id: "f", label: "F", value: "KeyF", width: 1, gap: 0 },
    { id: "g", label: "G", value: "KeyG", width: 1, gap: 0 },
    { id: "h", label: "H", value: "KeyH", width: 1, gap: 0 },
    { id: "j", label: "J", value: "KeyJ", width: 1, gap: 0 },
    { id: "k", label: "K", value: "KeyK", width: 1, gap: 0 },
    { id: "l", label: "L", value: "KeyL", width: 1, gap: 0 },
    { id: "m", label: "M", value: "KeyM", width: 1, gap: 0 },
    { id: "ù", label: "Ù", value: "Quote", width: 1, gap: 0 },
    { id: "backslash", label: "*", value: "Backslash", width: 1, gap: 0 },
    { id: "home", label: "Home", value: "Home", width: 1, gap: 30 },
  ],

  // Row 5
  [
    { id: "shift", label: "Shift", value: "ShiftLeft", width: 2.25, gap: 0 },
    { id: "sign", label: "<", value: "sign", width: 1, gap: 0 },
    { id: "w", label: "W", value: "KeyW", width: 1, gap: 0 },
    { id: "x", label: "X", value: "KeyX", width: 1, gap: 0 },
    { id: "c", label: "C", value: "KeyC", width: 1, gap: 0 },
    { id: "v", label: "V", value: "KeyV", width: 1, gap: 0 },
    { id: "b", label: "B", value: "KeyB", width: 1, gap: 0 },
    { id: "n", label: "N", value: "KeyN", width: 1, gap: 0 },
    { id: "comma", label: ",", value: "Comma", width: 1, gap: 0 },
    { id: "semicolon", label: ";", value: "Semicolon", width: 1, gap: 0 },
    { id: "colon", label: ":", value: "Period", width: 1, gap: 0 },
    { id: "excl", label: "!", value: "Slash", width: 1, gap: 0 },
    { id: "shift_r", label: "Shift", value: "ShiftRight", width: 1.8, gap: 0 },
    { id: "up", label: "↑", value: "ArrowUp", width: 1, gap: 0 },
  ],

  // Row 6
  [
    { id: "ctrl", label: "Ctrl", value: "ControlLeft", width: 1.25, gap: 0 },
    { id: "win", label: <IconBrandWindows />, value: "win", width: 1.25, gap: 0 },
    { id: "alt", label: "Alt", value: "AltLeft", width: 1.25, gap: 0 },
    { id: "space", label: " ", value: "Space", width: 6.25, gap: 0 },
    { id: "altgr", label: "AltGr", value: "AltRight", width: 1.25, gap: 0 },
    { id: "fn", label: "Fn", value: "Fn", width: 1.25, gap: 0.5 },
    { id: "ctrl_r", label: "Ctrl", value: "ControlRight", width: 1.25, gap: 0 },
    { id: "left", label: "←", value: "ArrowLeft", width: 1, gap: 3.2 },
    { id: "down", label: "↓", value: "ArrowDown", width: 1, gap: 0 },
    { id: "right", label: "→", value: "ArrowRight", width: 1, gap: 0 },
  ],
]

export default function BaseKeyboard() {
  return (
    <div className="flex flex-col gap-2 p-4 bg-white rounded-lg border border-gray-200 w-fit">
      {keyboard75Layout.map((row, rowIndex) => (
        <div key={rowIndex} className="flex gap-1">
          {row.map((keyData) => (
            <div key={keyData.id} style={{ marginLeft: `${keyData.gap * 0.25}rem` }}>
              {keyData.id === "enter" ? (
                <KeyEnter
                  id={keyData.id}
                  label={keyData.label}
                  width={keyData.width}
                  value={keyData.value}
                />
              ) : (
                <Key
                  id={keyData.id}
                  label={keyData.label}
                  width={keyData.width}
                  value={keyData.value}
                />
              )}
            </div>
          ))}
        </div>
      ))}
    </div>
  )
}