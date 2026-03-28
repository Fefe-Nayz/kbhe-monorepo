import time

def perform_firmware_update(device, firmware_path, firmware_version=None,
                            timeout_s=5.0, retries=5, reconnect_after=True, logger=None):
    import firmware_updater

    if firmware_version is None:
        firmware_version = firmware_updater.read_default_fw_version()

    log = logger or print
    log(
        f"Flashing {firmware_path} with firmware version "
        f"{firmware_updater.format_fw_version(firmware_version)} "
        f"(0x{firmware_version:04X})"
    )

    if device is not None:
        device.disconnect()

    firmware_updater.flash_firmware(
        firmware_path,
        firmware_version,
        timeout_s,
        retries,
        logger=log,
    )

    return firmware_version


def reconnect_device(device, timeout_s=5.0, logger=None):
    log = logger or print
    deadline = time.time() + timeout_s
    last_error = None

    device.disconnect()

    while time.time() < deadline:
        try:
            device.connect(logger=log)
            log("Reconnected to application Raw HID interface.")
            return True
        except Exception as exc:
            last_error = exc
            time.sleep(0.2)

    if last_error is not None:
        raise last_error

    raise RuntimeError("device did not reconnect")
