import Key from "./keyboard-components/key"

const keyboard75Layout = [
  // Row 1
  [
    { id: "esc", label: "Esc", value: "Escape", width: 1 },
    { id: "f1", label: "F1", value: "F1", width: 1 },
    { id: "f2", label: "F2", value: "F2", width: 1 },
    { id: "f3", label: "F3", value: "F3", width: 1 },
    { id: "f4", label: "F4", value: "F4", width: 1 },
    { id: "f5", label: "F5", value: "F5", width: 1 },
    { id: "f6", label: "F6", value: "F6", width: 1 },
    { id: "f7", label: "F7", value: "F7", width: 1 },
    { id: "f8", label: "F8", value: "F8", width: 1 },
    { id: "f9", label: "F9", value: "F9", width: 1 },
    { id: "f10", label: "F10", value: "F10", width: 1 },
    { id: "f11", label: "F11", value: "F11", width: 1 },
    { id: "f12", label: "F12", value: "F12", width: 1 },
    { id: "del", label: "Del", value: "Delete", width: 1 },
  ],
  // Row 2
  [
    { id: "grave", label: "`", value: "Backquote", width: 1 },
    { id: "1", label: "1", value: "Digit1", width: 1 },
    { id: "2", label: "2", value: "Digit2", width: 1 },
    { id: "3", label: "3", value: "Digit3", width: 1 },
    { id: "4", label: "4", value: "Digit4", width: 1 },
    { id: "5", label: "5", value: "Digit5", width: 1 },
    { id: "6", label: "6", value: "Digit6", width: 1 },
    { id: "7", label: "7", value: "Digit7", width: 1 },
    { id: "8", label: "8", value: "Digit8", width: 1 },
    { id: "9", label: "9", value: "Digit9", width: 1 },
    { id: "0", label: "0", value: "Digit0", width: 1 },
    { id: "minus", label: "-", value: "Minus", width: 1 },
    { id: "equal", label: "=", value: "Equal", width: 1 },
    { id: "backspace", label: "Backspace", value: "Backspace", width: 1 },
  ],
  // Row 3
  [
    { id: "tab", label: "Tab", value: "Tab", width: 1.5 },
    { id: "q", label: "Q", value: "KeyQ", width: 1 },
    { id: "w", label: "W", value: "KeyW", width: 1 },
    { id: "e", label: "E", value: "KeyE", width: 1 },
    { id: "r", label: "R", value: "KeyR", width: 1 },
    { id: "t", label: "T", value: "KeyT", width: 1 },
    { id: "y", label: "Y", value: "KeyY", width: 1 },
    { id: "u", label: "U", value: "KeyU", width: 1 },
    { id: "i", label: "I", value: "KeyI", width: 1 },
    { id: "o", label: "O", value: "KeyO", width: 1 },
    { id: "p", label: "P", value: "KeyP", width: 1 },
    { id: "lbracket", label: "[", value: "BracketLeft", width: 1 },
    { id: "rbracket", label: "]", value: "BracketRight", width: 1 },
    { id: "backslash", label: "\\", value: "Backslash", width: 1.5 },
  ],
  // Row 4
  [
    { id: "capslock", label: "Caps", value: "CapsLock", width: 1.75 },
    { id: "a", label: "A", value: "KeyA", width: 1 },
    { id: "s", label: "S", value: "KeyS", width: 1 },
    { id: "d", label: "D", value: "KeyD", width: 1 },
    { id: "f", label: "F", value: "KeyF", width: 1 },
    { id: "g", label: "G", value: "KeyG", width: 1 },
    { id: "h", label: "H", value: "KeyH", width: 1 },
    { id: "j", label: "J", value: "KeyJ", width: 1 },
    { id: "k", label: "K", value: "KeyK", width: 1 },
    { id: "l", label: "L", value: "KeyL", width: 1 },
    { id: "semicolon", label: ";", value: "Semicolon", width: 1 },
    { id: "quote", label: "'", value: "Quote", width: 1 },
    { id: "enter", label: "Enter", value: "Enter", width: 2.25 },
  ],
  // Row 5
  [
    { id: "shift", label: "Shift", value: "ShiftLeft", width: 2.25 },
    { id: "z", label: "Z", value: "KeyZ", width: 1 },
    { id: "x", label: "X", value: "KeyX", width: 1 },
    { id: "c", label: "C", value: "KeyC", width: 1 },
    { id: "v", label: "V", value: "KeyV", width: 1 },
    { id: "b", label: "B", value: "KeyB", width: 1 },
    { id: "n", label: "N", value: "KeyN", width: 1 },
    { id: "m", label: "M", value: "KeyM", width: 1 },
    { id: "comma", label: ",", value: "Comma", width: 1 },
    { id: "period", label: ".", value: "Period", width: 1 },
    { id: "slash", label: "/", value: "Slash", width: 1 },
    { id: "shift_r", label: "Shift", value: "ShiftRight", width: 1.75 },
  ],
  // Row 6
  [
    { id: "ctrl", label: "Ctrl", value: "ControlLeft", width: 1.25 },
    { id: "alt", label: "Alt", value: "AltLeft", width: 1.25 },
    { id: "space", label: " ", value: "Space", width: 6.25 },
    { id: "altgr", label: "AltGr", value: "AltRight", width: 1.25 },
    { id: "ctrl_r", label: "Ctrl", value: "ControlRight", width: 1.25 },
  ],
]

export default function BaseKeyboard() {
  return (
    <div className="flex flex-col gap-2 p-4 bg-white rounded-lg w-fit">
      {keyboard75Layout.map((row, rowIndex) => (
        <div key={rowIndex} className="flex gap-1">
          {row.map((keyData) => (
            <Key
              key={keyData.id}
              id={keyData.id}
              label={keyData.label}
              width={keyData.width}
              value={keyData.value}
              
            />
          ))}
        </div>
      ))}
    </div>
  )
}