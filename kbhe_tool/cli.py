import argparse
import sys

from .firmware import perform_firmware_update, reconnect_device
from .gui import HAS_GUI, launch_gui


def print_status(device):
    print("\n--- Device Status ---")

    version = device.get_firmware_version()
    print(f"Firmware Version: {version if version else 'Unknown'}")

    options = device.get_options()
    if options:
        print(f"Keyboard Enabled: {'Yes' if options['keyboard_enabled'] else 'No'}")
        print(f"Gamepad Enabled:  {'Yes' if options['gamepad_enabled'] else 'No'}")

    led_enabled = device.led_get_enabled()
    brightness = device.led_get_brightness()
    print(f"LED Enabled:      {'Yes' if led_enabled else 'No'}")
    print(f"LED Brightness:   {brightness if brightness is not None else 'Unknown'}")

    adc = device.get_adc_values()
    if adc:
        print(f"ADC Values: {adc}")

def interactive_menu(device):
    while True:
        print("\n=== KBHE Configuration Menu ===")
        print("1. Open Keyboard Configurator (GUI)")
        print("2. Show status")
        print("3. Toggle keyboard (enabled/disabled)")
        print("4. Toggle gamepad (enabled/disabled)")
        print("5. Toggle LED matrix (enabled/disabled)")
        print("6. Set LED brightness")
        print("7. LED test (rainbow)")
        print("8. LED clear")
        print("9. Save settings to flash")
        print("10. Firmware update")
        print("0. Exit")

        choice = input("\nChoice: ").strip()

        if choice == '1':
            if HAS_GUI:
                launch_gui(device)
            else:
                print('❌ GUI not available (PySide6 not installed)')
        elif choice == '2':
            print_status(device)
        elif choice == '3':
            options = device.get_options()
            if options:
                new_state = not options['keyboard_enabled']
                if device.set_keyboard_enabled(new_state):
                    print(f"✅ Keyboard {'enabled' if new_state else 'disabled'}")
                else:
                    print('❌ Failed')
        elif choice == '4':
            options = device.get_options()
            if options:
                new_state = not options['gamepad_enabled']
                if device.set_gamepad_enabled(new_state):
                    print(f"✅ Gamepad {'enabled' if new_state else 'disabled'}")
                else:
                    print('❌ Failed')
        elif choice == '5':
            led_enabled = device.led_get_enabled()
            if led_enabled is not None:
                new_state = not led_enabled
                if device.led_set_enabled(new_state):
                    print(f"✅ LED matrix {'enabled' if new_state else 'disabled'}")
                else:
                    print('❌ Failed')
        elif choice == '6':
            try:
                brightness = int(input('Brightness (0-255): ').strip())
                if device.led_set_brightness(brightness):
                    print(f'✅ Brightness set to {brightness}')
                else:
                    print('❌ Failed')
            except ValueError:
                print('Invalid input')
        elif choice == '7':
            print('✅ Rainbow test pattern displayed' if device.led_test_rainbow() else '❌ Failed')
        elif choice == '8':
            print('✅ LEDs cleared' if device.led_clear() else '❌ Failed')
        elif choice == '9':
            print('✅ Settings saved to flash' if device.save_settings() else '❌ Failed to save')
        elif choice == '10':
            firmware_path = input('Firmware .bin path: ').strip().strip('"')
            if firmware_path:
                try:
                    perform_firmware_update(None, firmware_path)
                    reconnect_device(device)
                    print_status(device)
                except Exception as exc:
                    print(f'❌ Firmware update failed: {exc}')
                    break
        elif choice == '0':
            break
        else:
            print('Invalid choice')


def parse_args():
    parser = argparse.ArgumentParser(description='KBHE Raw HID configuration tool')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--gui', action='store_true', help='Launch the PySide6 configurator')
    group.add_argument('--flash', metavar='FIRMWARE_BIN', help='Flash a firmware .bin over HS HID updater')
    parser.add_argument('--fw-version', type=lambda value: int(value, 0), default=None)
    parser.add_argument('--timeout', type=float, default=5.0)
    parser.add_argument('--retries', type=int, default=5)
    return parser.parse_args()


def main():
    args = parse_args()
    print('=== KBHE Raw HID Configuration Tool ===\n')

    if args.retries < 1:
        print('❌ Error: --retries must be at least 1')
        return 1

    try:
        from .device import KBHEDevice
    except ModuleNotFoundError as ex:
        if ex.name == "hid":
            print("❌ Error: missing Python package 'hid'. Install it with `pip install hidapi`.")
            return 1
        raise

    if args.flash:
        device = KBHEDevice()
        try:
            perform_firmware_update(
                None,
                args.flash,
                firmware_version=args.fw_version,
                timeout_s=args.timeout,
                retries=args.retries,
                reconnect_after=False,
            )
            reconnect_device(device, timeout_s=args.timeout)
            print_status(device)
        except Exception as ex:
            print(f'❌ Error: {ex}')
            return 1
        finally:
            device.disconnect()
            print('\nDisconnected.')
        return 0

    device = KBHEDevice()
    try:
        device.connect()
        print_status(device)
        if args.gui:
            if HAS_GUI:
                launch_gui(device)
            else:
                print('❌ GUI not available')
                interactive_menu(device)
        else:
            interactive_menu(device)
    except Exception as ex:
        print(f'❌ Error: {ex}')
        return 1
    finally:
        device.disconnect()
        print('\nDisconnected.')

    return 0


__all__ = ['main']
