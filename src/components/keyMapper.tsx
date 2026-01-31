import { Input } from "./ui/input";
import { ScrollArea } from "@/components/ui/scroll-area"


import Key from "./keyboard-components/key"
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion"

import { useState } from "react";

const AllKeys = [

  { id: "f1", label: "F1", value: "F1", width: 1, type: "Basic" },
  { id: "f2", label: "F2", value: "F2", width: 1, type: "Basic" },
  { id: "f3", label: "F3", value: "F3", width: 1, type: "Basic" },
  { id: "f4", label: "F4", value: "F4", width: 1, type: "Basic" },
  { id: "f5", label: "F5", value: "F5", width: 1, type: "Basic" },
  { id: "f6", label: "F6", value: "F6", width: 1, type: "Basic" },
  { id: "f7", label: "F7", value: "F7", width: 1, type: "Basic" },
  { id: "f8", label: "F8", value: "F8", width: 1, type: "Basic" },
  { id: "f9", label: "F9", value: "F9", width: 1, type: "Basic" },
  { id: "f10", label: "F10", value: "F10", width: 1, type: "Basic" },
  { id: "f11", label: "F11", value: "F11", width: 1, type: "Basic" },
  { id: "f12", label: "F12", value: "F12", width: 1, type: "Basic" },


  { id: "grave", label: "²", value: "Backquote", width: 1, type: "Basic" },
  { id: "1", label: "&", value: "Digit1", width: 1, type: "Basic" },
  { id: "2", label: "é", value: "Digit2", width: 1, type: "Basic" },
  { id: "3", label: "\"", value: "Digit3", width: 1, type: "Basic" },
  { id: "4", label: "'", value: "Digit4", width: 1, type: "Basic" },
  { id: "5", label: "(", value: "Digit5", width: 1, type: "Basic" },
  { id: "6", label: "-", value: "Digit6", width: 1, type: "Basic" },
  { id: "7", label: "è", value: "Digit7", width: 1, type: "Basic" },
  { id: "8", label: "_", value: "Digit8", width: 1, type: "Basic" },
  { id: "9", label: "ç", value: "Digit9", width: 1, type: "Basic" },
  { id: "0", label: "à", value: "Digit0", width: 1, type: "Basic" },
  { id: "minus", label: ")", value: "Minus", width: 1, type: "Basic" },
  { id: "equal", label: "=", value: "Equal", width: 1, type: "Basic" },


  { id: "tab", label: "Tab", value: "Tab", width: 1, type: "Basic" },
  { id: "a", label: "A", value: "KeyA", width: 1, type: "Basic" },
  { id: "z", label: "Z", value: "KeyZ", width: 1, type: "Basic" },
  { id: "e", label: "E", value: "KeyE", width: 1, type: "Basic" },
  { id: "r", label: "R", value: "KeyR", width: 1, type: "Basic" },
  { id: "t", label: "T", value: "KeyT", width: 1, type: "Basic" },
  { id: "y", label: "Y", value: "KeyY", width: 1, type: "Basic" },
  { id: "u", label: "U", value: "KeyU", width: 1, type: "Basic" },
  { id: "i", label: "I", value: "KeyI", width: 1, type: "Basic" },
  { id: "o", label: "O", value: "KeyO", width: 1, type: "Basic" },
  { id: "p", label: "P", value: "KeyP", width: 1, type: "Basic" },
  { id: "lbracket", label: "^", value: "BracketLeft", width: 1, type: "Basic" },
  { id: "rbracket", label: "$", value: "BracketRight", width: 1, type: "Basic" },
  { id: "capslock", label: "Caps", value: "CapsLock", width: 1, type: "Basic" },
  { id: "q", label: "Q", value: "KeyQ", width: 1, type: "Basic" },
  { id: "s", label: "S", value: "KeyS", width: 1, type: "Basic" },
  { id: "d", label: "D", value: "KeyD", width: 1, type: "Basic" },
  { id: "f", label: "F", value: "KeyF", width: 1, type: "Basic" },
  { id: "g", label: "G", value: "KeyG", width: 1, type: "Basic" },
  { id: "h", label: "H", value: "KeyH", width: 1, type: "Basic" },
  { id: "j", label: "J", value: "KeyJ", width: 1, type: "Basic" },
  { id: "k", label: "K", value: "KeyK", width: 1, type: "Basic" },
  { id: "l", label: "L", value: "KeyL", width: 1, type: "Basic" },
  { id: "m", label: "M", value: "KeyM", width: 1, type: "Basic" },
  { id: "ù", label: "Ù", value: "Quote", width: 1, type: "Basic" },

  { id: "esc", label: "Esc", value: "Escape", width: 1, type: "Extended" },
  { id: "shift", label: "Shift", value: "ShiftLeft", width: 1, type: "Extended" },
  { id: "shift_r", label: "Shift", value: "ShiftRight", width: 1, type: "Extended" },
  { id: "ctrl", label: "Ctrl", value: "ControlLeft", width: 1, type: "Extended" },
  { id: "alt", label: "Alt", value: "AltLeft", width: 1, type: "Extended" },
  { id: "altgr", label: "AltGr", value: "AltRight", width: 1, type: "Extended" },
  { id: "win", label: "Win", value: "win", width: 1, type: "Extended" },
  { id: "fn", label: "Fn", value: "Fn", width: 1, type: "Extended" },


  { id: "space", label: "Space", value: "Space", width: 1, type: "Basic" },
  { id: "enter", label: "Enter", value: "Enter", width: 1, type: "Extended" },
  { id: "backspace", label: "Backspace", value: "Backspace", width: 1, type: "Extended" },


  { id: "pgup", label: "PgUp", value: "PageUp", width: 1, type: "Extended" },
  { id: "pgdn", label: "PgDn", value: "PageDown", width: 1, type: "Extended" },
  { id: "home", label: "Home", value: "Home", width: 1, type: "Extended" },
  { id: "up", label: "↑", value: "ArrowUp", width: 1, type: "Extended" },
  { id: "left", label: "←", value: "ArrowLeft", width: 1, type: "Extended" },
  { id: "down", label: "↓", value: "ArrowDown", width: 1, type: "Extended" },
  { id: "right", label: "→", value: "ArrowRight", width: 1, type: "Extended" },


  { id: "comma", label: ",", value: "Comma", width: 1, type: "Basic" },
  { id: "semicolon", label: ";", value: "Semicolon", width: 1, type: "Basic" },
  { id: "colon", label: ":", value: "Period", width: 1, type: "Basic" },
  { id: "excl", label: "!", value: "Slash", width: 1, type: "Basic" },
];



export default function KeyMapper() {

  const groupedKeys = AllKeys.reduce((acc, key) => {
    if (!acc[key.type]) {
      acc[key.type] = [];
    }
    acc[key.type].push(key);
    return acc;
  }, {} as Record<string, typeof AllKeys>);


  const funcFilter = (type: string, searchTerm: string, keys: typeof AllKeys) => {
    type
    if (searchTerm === "") {
      return true;
    }
    return keys.some(key => key.id.toLowerCase().includes(searchTerm.toLowerCase()))
  }

  const [searchTerm, setSearchTerm] = useState("");
  return (

    <main className="relative flex flex-wrap gap-1 p-4 bg-blue-500 border border-gray-200 w-full">
      <div className="flex justify-end w-full mb-2">
        <Input
          placeholder="Search for a component here"
          value={searchTerm}
          onChange={(event) => {
            setSearchTerm(event.target.value);
            console.log(event.target.value);
          }} />
      </div>
      <ScrollArea className=" absolute h-45 w-full rounded-md border p-4">


        {Object.entries(groupedKeys).filter(([type, keys]) => funcFilter(type, searchTerm, keys)).map(([type, keys]) => (
          <Accordion defaultValue={["Basic"]} className="w-full mt-4">
            <AccordionItem value={type} disabled = {searchTerm !== "" ? true : false}>
              <AccordionTrigger >{type}</AccordionTrigger>
              <AccordionContent>
                {keys.filter(key => key.id.toLowerCase().includes(searchTerm.toLowerCase())).map((keyData) => (
                  <div className="inline-block m-0.5" key={keyData.id}>
                    <Key
                      key={keyData.id}
                      id={keyData.id}
                      label={keyData.label}
                      width={keyData.width}
                      value={keyData.value}

                    />

                  </div>

                ))}
              </AccordionContent>
            </AccordionItem>
          </Accordion>
        ))}



      </ScrollArea>

    </main>
  )
}