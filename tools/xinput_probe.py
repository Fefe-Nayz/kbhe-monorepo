import argparse
import ctypes
import time
from ctypes import wintypes


ERROR_SUCCESS = 0
ERROR_DEVICE_NOT_CONNECTED = 1167
XUSER_MAX_COUNT = 4


class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ("wButtons", wintypes.WORD),
        ("bLeftTrigger", wintypes.BYTE),
        ("bRightTrigger", wintypes.BYTE),
        ("sThumbLX", ctypes.c_short),
        ("sThumbLY", ctypes.c_short),
        ("sThumbRX", ctypes.c_short),
        ("sThumbRY", ctypes.c_short),
    ]


class XINPUT_STATE(ctypes.Structure):
    _fields_ = [
        ("dwPacketNumber", wintypes.DWORD),
        ("Gamepad", XINPUT_GAMEPAD),
    ]


BUTTONS = [
    ("DPAD_UP", 0x0001),
    ("DPAD_DOWN", 0x0002),
    ("DPAD_LEFT", 0x0004),
    ("DPAD_RIGHT", 0x0008),
    ("START", 0x0010),
    ("BACK", 0x0020),
    ("L3", 0x0040),
    ("R3", 0x0080),
    ("LB", 0x0100),
    ("RB", 0x0200),
    ("GUIDE", 0x0400),
    ("A", 0x1000),
    ("B", 0x2000),
    ("X", 0x4000),
    ("Y", 0x8000),
]


def load_xinput():
    for dll_name in ("xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"):
        try:
            dll = ctypes.WinDLL(dll_name)
            fn = dll.XInputGetState
            fn.argtypes = [wintypes.DWORD, ctypes.POINTER(XINPUT_STATE)]
            fn.restype = wintypes.DWORD
            return dll_name, fn
        except Exception:
            continue
    raise RuntimeError("No XInput DLL could be loaded")


def read_slot(get_state, slot):
    state = XINPUT_STATE()
    result = get_state(slot, ctypes.byref(state))
    if result == ERROR_SUCCESS:
        return state
    if result == ERROR_DEVICE_NOT_CONNECTED:
        return None
    raise RuntimeError(f"XInputGetState(slot={slot}) failed with error {result}")


def format_state(slot, state):
    pressed = [name for name, bit in BUTTONS if state.Gamepad.wButtons & bit]
    if not pressed:
        pressed = ["none"]
    return (
        f"slot {slot}: packet={state.dwPacketNumber} "
        f"buttons={','.join(pressed)} "
        f"LT={state.Gamepad.bLeftTrigger:3d} RT={state.Gamepad.bRightTrigger:3d} "
        f"LX={state.Gamepad.sThumbLX:6d} LY={state.Gamepad.sThumbLY:6d} "
        f"RX={state.Gamepad.sThumbRX:6d} RY={state.Gamepad.sThumbRY:6d}"
    )


def probe_once(get_state):
    found = False
    for slot in range(XUSER_MAX_COUNT):
        state = read_slot(get_state, slot)
        if state is None:
            continue
        found = True
        print(format_state(slot, state))
    if not found:
        print("No XInput controller detected on slots 0-3.")
    return found


def main():
    parser = argparse.ArgumentParser(
        description="Probe real XInput visibility on Windows."
    )
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Poll continuously so you can press buttons and see live changes.",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.25,
        help="Polling interval in seconds for --watch.",
    )
    args = parser.parse_args()

    dll_name, get_state = load_xinput()
    print(f"Loaded {dll_name}")

    if not args.watch:
        probe_once(get_state)
        return

    print("Watching XInput slots 0-3. Press Ctrl+C to stop.")
    last_lines = {}
    try:
        while True:
            any_connected = False
            for slot in range(XUSER_MAX_COUNT):
                state = read_slot(get_state, slot)
                if state is None:
                    if last_lines.get(slot) is not None:
                        print(f"slot {slot}: disconnected")
                        last_lines[slot] = None
                    continue
                any_connected = True
                line = format_state(slot, state)
                if last_lines.get(slot) != line:
                    print(line)
                    last_lines[slot] = line
            if not any_connected and last_lines.get("empty") != "empty":
                print("No XInput controller detected on slots 0-3.")
                last_lines["empty"] = "empty"
            elif any_connected:
                last_lines["empty"] = None
            time.sleep(max(0.02, args.interval))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
