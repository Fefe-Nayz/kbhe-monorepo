#include "settings.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  USB_DESC_CONFIGURATION = 0x02u,
  USB_DESC_INTERFACE = 0x04u,
  USB_DESC_ENDPOINT = 0x05u,
  USB_DESC_HID_OR_XUSB = 0x21u,
  USB_DESC_HID_REPORT = 0x22u,
  USB_DESC_HID_LENGTH = 9u,
  USB_CLASS_HID = 0x03u,
  USB_CLASS_VENDOR = 0xFFu,
  GAMEPAD_INTERFACE_NUMBER = 5u,
};

static gamepad_api_mode_t s_gamepad_api_mode = GAMEPAD_API_HID;
static void *s_control_buffer = NULL;
static uint16_t s_control_length = 0u;

gamepad_api_mode_t settings_get_gamepad_api_mode(void) {
  return s_gamepad_api_mode;
}

bool tud_control_xfer(uint8_t rhport,
                      tusb_control_request_t const *request, void *buffer,
                      uint16_t len) {
  (void)rhport;
  (void)request;
  s_control_buffer = buffer;
  s_control_length = len;
  return true;
}

static uint16_t read_u16_le(const uint8_t *bytes) {
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

static bool contains_bytes(const uint8_t *haystack, size_t haystack_length,
                           const uint8_t *needle, size_t needle_length) {
  if (needle_length == 0u || needle_length > haystack_length) {
    return false;
  }

  for (size_t offset = 0u; offset <= haystack_length - needle_length;
       offset++) {
    if (memcmp(&haystack[offset], needle, needle_length) == 0) {
      return true;
    }
  }
  return false;
}

static void assert_configuration_identity(gamepad_api_mode_t mode) {
  static const uint8_t gamepad_application_collection[] = {
      0x05u, 0x01u, 0x09u, 0x05u, 0xA1u, 0x01u};
  const uint8_t *configuration = NULL;
  uint16_t total_length = 0u;
  uint16_t offset = 0u;
  uint8_t current_interface_class = 0u;
  uint8_t interface_count = 0u;
  uint8_t hid_interface_count = 0u;
  uint8_t hid_instance = 0u;
  uint8_t xinput_interface_count = 0u;
  uint8_t hid_gamepad_interface_count = 0u;
  uint8_t gamepad_application_collection_count = 0u;

  s_gamepad_api_mode = mode;
  configuration = tud_descriptor_configuration_cb(0u);
  assert(configuration != NULL);
  assert(configuration[0] == TUD_CONFIG_DESC_LEN);
  assert(configuration[1] == USB_DESC_CONFIGURATION);
  total_length = read_u16_le(&configuration[2]);
  assert(configuration[4] == 6u);

  while (offset < total_length) {
    const uint8_t descriptor_length = configuration[offset];
    const uint8_t descriptor_type = configuration[offset + 1u];

    assert(descriptor_length >= 2u);
    assert((uint16_t)(offset + descriptor_length) <= total_length);

    if (descriptor_type == USB_DESC_INTERFACE) {
      const uint8_t interface_number = configuration[offset + 2u];
      const uint8_t endpoint_count = configuration[offset + 4u];
      const uint8_t interface_class = configuration[offset + 5u];
      const uint8_t interface_subclass = configuration[offset + 6u];
      const uint8_t interface_protocol = configuration[offset + 7u];
      const uint8_t string_index = configuration[offset + 8u];

      interface_count++;
      current_interface_class = interface_class;
      if (interface_class == USB_CLASS_HID) {
        hid_interface_count++;
        if (interface_number == GAMEPAD_INTERFACE_NUMBER) {
          hid_gamepad_interface_count++;
          assert(string_index == STRID_GAMEPAD);
        }
      }

      if (interface_class == USB_CLASS_VENDOR &&
          interface_subclass == XINPUT_SUBCLASS_DEFAULT &&
          interface_protocol == XINPUT_PROTOCOL_DEFAULT) {
        xinput_interface_count++;
        assert(interface_number == GAMEPAD_INTERFACE_NUMBER);
        assert(endpoint_count == 2u);
        assert(string_index == STRID_XINPUT);
        assert((uint16_t)(offset + descriptor_length + 1u) < total_length);
        assert(configuration[offset + descriptor_length] == 16u);
        assert(configuration[offset + descriptor_length + 1u] ==
               USB_DESC_HID_OR_XUSB);
      }
    }

    if (descriptor_type == USB_DESC_HID_OR_XUSB &&
        current_interface_class == USB_CLASS_HID) {
      const uint8_t *report = NULL;
      uint16_t report_length = 0u;

      assert(descriptor_length == USB_DESC_HID_LENGTH);
      assert(configuration[offset + 6u] == USB_DESC_HID_REPORT);
      report_length = read_u16_le(&configuration[offset + 7u]);
      report = tud_hid_descriptor_report_cb(hid_instance);
      assert(report != NULL);
      if (contains_bytes(report, report_length, gamepad_application_collection,
                         sizeof(gamepad_application_collection))) {
        gamepad_application_collection_count++;
      }
      hid_instance++;
    }

    if (descriptor_type == USB_DESC_ENDPOINT) {
      const uint8_t endpoint = configuration[offset + 2u];
      if (endpoint == EPNUM_XINPUT_IN || endpoint == EPNUM_XINPUT_OUT) {
        assert(mode == GAMEPAD_API_XINPUT);
      }
    }

    offset = (uint16_t)(offset + descriptor_length);
  }

  assert(offset == total_length);
  assert(interface_count == 6u);
  assert(hid_instance == hid_interface_count);
  if (mode == GAMEPAD_API_XINPUT) {
    assert(hid_interface_count == 5u);
    assert(hid_gamepad_interface_count == 0u);
    assert(gamepad_application_collection_count == 0u);
    assert(xinput_interface_count == 1u);
  } else {
    assert(hid_interface_count == 6u);
    assert(hid_gamepad_interface_count == 1u);
    assert(gamepad_application_collection_count == 1u);
    assert(xinput_interface_count == 0u);
  }
}

static void assert_device_identity(gamepad_api_mode_t mode,
                                   uint16_t expected_bcd_usb,
                                   uint16_t expected_bcd_device) {
  const tusb_desc_device_t *device = NULL;

  s_gamepad_api_mode = mode;
  device = (const tusb_desc_device_t *)tud_descriptor_device_cb();
  assert(device != NULL);
  assert(device->bcdUSB == expected_bcd_usb);
  assert(device->bcdDevice == expected_bcd_device);
  assert(device->idVendor == USB_VID);
  assert(device->idProduct == USB_PID);
}

static void assert_xinput_compatible_id(void) {
  static const uint8_t xusb10[] = {'X', 'U', 'S', 'B', '1', '0', 0u, 0u};
  static const uint8_t xusb20[] = {'X', 'U', 'S', 'B', '2', '0', 0u, 0u};
  tusb_control_request_t request = {0};

  request.bmRequestType_bit.type = TUSB_REQ_TYPE_VENDOR;
  request.bRequest = 0x01u;
  request.wIndex = 0x0007u;

  s_gamepad_api_mode = GAMEPAD_API_HID;
  assert(!tud_vendor_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));

  s_gamepad_api_mode = GAMEPAD_API_XINPUT;
  s_control_buffer = NULL;
  s_control_length = 0u;
  assert(tud_vendor_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
  assert(s_control_buffer != NULL);
  assert(s_control_length != 0u);
  assert(contains_bytes((const uint8_t *)s_control_buffer, s_control_length,
                        xusb10, sizeof(xusb10)));
  assert(!contains_bytes((const uint8_t *)s_control_buffer, s_control_length,
                         xusb20, sizeof(xusb20)));
}

int main(void) {
  assert_configuration_identity(GAMEPAD_API_HID);
  assert_configuration_identity(GAMEPAD_API_XINPUT);
  assert_device_identity(GAMEPAD_API_HID, 0x0200u, 0x0104u);
  assert_device_identity(GAMEPAD_API_XINPUT, 0x0210u, 0x0106u);
  assert_xinput_compatible_id();

  puts("usb_gamepad_identity_test: ok");
  return 0;
}
