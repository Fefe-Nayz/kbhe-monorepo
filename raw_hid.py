import hid
import time

# Modifier ces valeurs selon votre descripteur USB
VENDOR_ID = 0x1209  # VID utilisé dans usb_descriptors.h
PRODUCT_ID = 0x0001 # PID utilisé dans usb_descriptors.h
RAW_HID_REPORT_SIZE = 64

# Trouver le périphérique
for d in hid.enumerate():
    if d['vendor_id'] == VENDOR_ID and d['product_id'] == PRODUCT_ID:
        print(f"Found device: {d['product_string']} (path: {d['path']})")
        device_path = d['path']
        break
else:
    print("Device not found!")
    exit(1)

# Ouvrir le périphérique
h = hid.device()
h.open_path(device_path)
h.set_nonblocking(1)

# Exemple : envoyer un buffer et attendre l'echo
send_data = bytes([0x42] + [i & 0xFF for i in range(1, RAW_HID_REPORT_SIZE)])
print(f"Sending: {send_data.hex()}")
h.write(send_data)

# Attendre la réponse (echo)
for _ in range(100):
    data = h.read(RAW_HID_REPORT_SIZE, timeout_ms=100)
    if data:
        print(f"Received: {bytes(data).hex()}")
        break
    time.sleep(0.01)
else:
    print("No response received!")

h.close()
