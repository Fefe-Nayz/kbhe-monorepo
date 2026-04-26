import type { ComponentType, ReactNode } from "react";
import {
    IconArrowDown,
    IconArrowLeft,
    IconArrowRight,
    IconArrowUp,
    IconBackspace,
    IconBrandWindows,
    IconBrightnessDown,
    IconBrightnessUp,
    IconBulb,
    IconCalculator,
    IconChevronsDown,
    IconChevronsLeft,
    IconChevronsRight,
    IconChevronsUp,
    IconCornerDownLeft,
    IconDeviceDesktop,
    IconDeviceGamepad,
    IconDeviceGamepad2,
    IconHome,
    IconLadder,
    IconLayersDifference,
    IconLayersIntersect,
    IconLock,
    IconLockSquareRounded,
    IconMail,
    IconMenu2,
    IconMouse,
    IconMouse2,
    IconPalette,
    IconPlayerEject,
    IconPlayerPause,
    IconPlayerPlay,
    IconPlayerStop,
    IconPlayerTrackNext,
    IconPlayerTrackPrev,
    IconPower,
    IconRefresh,
    IconSettings,
    IconSpace,
    IconSquareRoundedArrowDown,
    IconSquareRoundedArrowLeft,
    IconSquareRoundedArrowRight,
    IconSquareRoundedArrowUp,
    IconSquareRoundedLetterA,
    IconSquareRoundedLetterC,
    IconSquareRoundedLetterD,
    IconSquareRoundedLetterE,
    IconSquareRoundedLetterI,
    IconStar,
    IconSwitch2,
    IconSwitch3,
    IconTrash,
    IconVolume2,
    IconVolume,
    IconVolumeOff,
    IconWorldSearch,
    IconWorldWww,
    IconXboxA,
    IconXboxB,
    IconXboxX,
    IconXboxY,
} from "@tabler/icons-react";
import { HID_KEYCODE_NAMES } from "@/lib/kbhe/protocol";

export type KeycodeIconComponent = ComponentType<{ className?: string }>;

const KEYCODE_ICON_OVERRIDES: Record<string, KeycodeIconComponent> = {
    // Navigation and basic editing
    UP: IconArrowUp,
    DOWN: IconArrowDown,
    LEFT: IconArrowLeft,
    RIGHT: IconArrowRight,
    ENTER: IconCornerDownLeft,
    BACKSPACE: IconBackspace,
    TAB: IconChevronsRight,
    SPACE: IconSpace,
    CAPSLOCK: IconLockSquareRounded,
    NUMLOCK: IconLock,
    SCROLLLOCK: IconLock,
    INSERT: IconSquareRoundedLetterI,
    DELETE: IconSquareRoundedLetterD,
    HOME: IconDeviceDesktop,
    END: IconSquareRoundedLetterE,
    PAGEUP: IconChevronsUp,
    PAGEDOWN: IconChevronsDown,
    PRINTSCREEN: IconDeviceDesktop,
    PAUSE: IconPlayerPause,

    // Modifiers
    LSHIFT: IconSquareRoundedArrowUp,
    RSHIFT: IconSquareRoundedArrowUp,
    LCTRL: IconSquareRoundedLetterC,
    RCTRL: IconSquareRoundedLetterC,
    LALT: IconSquareRoundedLetterA,
    RALT: IconSquareRoundedLetterA,
    LGUI: IconBrandWindows,
    RGUI: IconBrandWindows,
    APPLICATION: IconMenu2,

    // Consumer/media/system
    MEDIA_PLAY_PAUSE: IconPlayerPause,
    MEDIA_NEXT_TRACK: IconPlayerTrackNext,
    MEDIA_PREV_TRACK: IconPlayerTrackPrev,
    MEDIA_STOP: IconPlayerStop,
    MEDIA_SELECT: IconPlayerPlay,
    MEDIA_EJECT: IconPlayerEject,
    AUDIO_MUTE: IconVolumeOff,
    MUTE: IconVolumeOff,
    AUDIO_VOL_UP: IconVolume,
    AUDIO_VOL_DOWN: IconVolume2,
    VOLUMEUP: IconVolume,
    VOLUMEDOWN: IconVolume2,
    MAIL: IconMail,
    CALCULATOR: IconCalculator,
    MY_COMPUTER: IconDeviceDesktop,
    WWW_SEARCH: IconWorldSearch,
    WWW_HOME: IconWorldWww,
    WWW_BACK: IconArrowLeft,
    WWW_FORWARD: IconArrowRight,
    WWW_REFRESH: IconRefresh,
    WWW_FAVORITES: IconStar,
    BRIGHTNESS_UP: IconBrightnessUp,
    BRIGHTNESS_DOWN: IconBrightnessDown,
    CONTROL_PANEL: IconSettings,
    KB_POWER: IconPower,

    // Layer and custom firmware keys
    FN_MO_LAYER_1: IconLayersIntersect,
    MO_LAYER_2: IconLayersDifference,
    MO_LAYER_3: IconLayersDifference,
    TG_LAYER_1: IconSwitch2,
    TG_LAYER_2: IconSwitch2,
    TG_LAYER_3: IconSwitch3,
    SET_BASE_LAYER: IconLadder,
    SET_FN_LAYER: IconLayersIntersect,
    SET_LAYER_2: IconLayersDifference,
    SET_LAYER_3: IconLayersDifference,
    CLEAR_LAYER_TOGGLES: IconTrash,

    // LED controls
    LED_TOGGLE: IconBulb,
    LED_BRIGHTNESS_DOWN: IconBrightnessDown,
    LED_BRIGHTNESS_UP: IconBrightnessUp,
    LED_EFFECT_PREV: IconPlayerTrackPrev,
    LED_EFFECT_NEXT: IconPlayerTrackNext,
    LED_SPEED_DOWN: IconChevronsDown,
    LED_SPEED_UP: IconChevronsUp,
    LED_COLOR_NEXT: IconPalette,

    // Mouse keys
    MOUSE_LEFT: IconMouse2,
    MOUSE_RIGHT: IconMouse2,
    MOUSE_MIDDLE: IconMouse,
    MOUSE_BACK: IconArrowLeft,
    MOUSE_FORWARD: IconArrowRight,
    MOUSE_WHEEL_UP: IconChevronsUp,
    MOUSE_WHEEL_DOWN: IconChevronsDown,
    MOUSE_WHEEL_LEFT: IconChevronsLeft,
    MOUSE_WHEEL_RIGHT: IconChevronsRight,

    // Gamepad controls
    GAMEPAD_ENABLE: IconDeviceGamepad2,
    GAMEPAD_DISABLE: IconDeviceGamepad,
    GAMEPAD_TOGGLE: IconSwitch2,
    GP_A: IconXboxA,
    GP_B: IconXboxB,
    GP_X: IconXboxX,
    GP_Y: IconXboxY,
    GP_LB: IconSquareRoundedLetterA,
    GP_RB: IconSquareRoundedLetterA,
    GP_LT_TRIGGER: IconSquareRoundedLetterA,
    GP_RT_TRIGGER: IconSquareRoundedLetterA,
    GP_BACK: IconArrowLeft,
    GP_START: IconPlayerPlay,
    GP_L3: IconSquareRoundedLetterI,
    GP_R3: IconSquareRoundedLetterI,
    GP_DPAD_UP: IconSquareRoundedArrowUp,
    GP_DPAD_DOWN: IconSquareRoundedArrowDown,
    GP_DPAD_LEFT: IconSquareRoundedArrowLeft,
    GP_DPAD_RIGHT: IconSquareRoundedArrowRight,
    GP_HOME: IconHome,
    GP_LS_RIGHT: IconArrowRight,
    GP_LS_LEFT: IconArrowLeft,
    GP_LS_DOWN: IconArrowDown,
    GP_LS_UP: IconArrowUp,
    GP_RS_RIGHT: IconChevronsRight,
    GP_RS_LEFT: IconChevronsLeft,
    GP_RS_DOWN: IconChevronsDown,
    GP_RS_UP: IconChevronsUp,
};

function normalizeKeycodeName(name: string): string {
    return name
        .trim()
        .toUpperCase()
        .replace(/[^A-Z0-9]+/g, "_")
        .replace(/^_+|_+$/g, "");
}

export function getKeycodeIconOverride(name: string): KeycodeIconComponent | undefined {
    return KEYCODE_ICON_OVERRIDES[name] ?? KEYCODE_ICON_OVERRIDES[normalizeKeycodeName(name)];
}

export function getKeycodeIconOverrideByCode(code: number): KeycodeIconComponent | undefined {
    const name = HID_KEYCODE_NAMES[code];
    if (!name) {
        return undefined;
    }

    return getKeycodeIconOverride(name);
}

function getSlotText(value: ReactNode | undefined): string | undefined {
    if (typeof value === "string") {
        const trimmed = value.trim();
        return trimmed.length > 0 ? trimmed : undefined;
    }

    if (typeof value === "number") {
        return String(value);
    }

    return undefined;
}

function buildLegendTooltipText(baseSlots: Array<ReactNode | undefined>): string | undefined {
    const orderedIndices = [0, 6, 2, 8];
    const values: string[] = [];

    for (const index of orderedIndices) {
        const text = getSlotText(baseSlots[index]);
        if (!text || values.includes(text)) {
            continue;
        }
        values.push(text);
    }

    if (values.length === 0) {
        for (const slot of baseSlots) {
            const text = getSlotText(slot);
            if (!text || values.includes(text)) {
                continue;
            }
            values.push(text);
        }
    }

    if (values.length === 0) {
        return undefined;
    }

    return values.join(" / ");
}

export function buildKeycodeLegendSlots(
    code: number,
    baseSlots: Array<ReactNode | undefined>,
    iconClassName = "size-3.5",
): Array<ReactNode | undefined> {
    const Icon = getKeycodeIconOverrideByCode(code);
    if (!Icon) {
        return baseSlots;
    }

    const slots: Array<ReactNode | undefined> = Array.from({ length: 12 }, () => "");
    const tooltipText = buildLegendTooltipText(baseSlots);
    slots[4] = (
        <Icon
            className={iconClassName}
            data-keycap-text={tooltipText}
            aria-label={tooltipText}
        />
    );
    return slots;
}
