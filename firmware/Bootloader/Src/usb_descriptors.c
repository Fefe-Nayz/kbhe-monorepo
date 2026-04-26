#include "usb_descriptors.h"

#include "stm32f7xx.h"
#include "tusb.h"

#include <string.h>

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(UPDATER_PACKET_SIZE)};

enum {
  ITF_NUM_UPDATER = 0,
  ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_UPDATER, STRID_UPDATER,
                             HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                             EPNUM_UPDATER_OUT, 0x80 | EPNUM_UPDATER_IN,
                             HID_EP_SIZE, UPDATER_POLL_INTERVAL),
};

#if TUD_OPT_HIGH_SPEED
static tusb_desc_device_qualifier_t const desc_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0x00,
};

static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN];
#endif

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "KBHE",
    "KBHE Updater",
    NULL,
    "Firmware Updater",
};

static uint16_t desc_str[32 + 1];

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  return (instance == HID_ITF_UPDATER) ? desc_hid_report : NULL;
}

#if TUD_OPT_HIGH_SPEED
uint8_t const *tud_descriptor_device_qualifier_cb(void) {
  return (uint8_t const *)&desc_device_qualifier;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
  (void)index;

  memcpy(desc_other_speed_config, desc_configuration, CONFIG_TOTAL_LEN);
  desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
  return desc_other_speed_config;
}
#endif

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  size_t chr_count;

  (void)langid;

  switch (index) {
  case STRID_LANGID:
    memcpy(&desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
    break;

  case STRID_SERIAL: {
    static const char hex_chars[] = "0123456789ABCDEF";
    volatile uint32_t *uid = (volatile uint32_t *)UID_BASE;

    chr_count = 24;
    for (uint8_t w = 0; w < 3; w++) {
      uint32_t value = uid[w];
      for (int8_t n = 7; n >= 0; n--) {
        desc_str[1 + w * 8 + (7 - n)] = hex_chars[(value >> (n * 4)) & 0x0F];
      }
    }
    break;
  }

  default: {
    const char *str;
    size_t max_count;

    if (!(index < (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))) {
      return NULL;
    }

    str = string_desc_arr[index];
    chr_count = strlen(str);
    max_count = (sizeof(desc_str) / sizeof(desc_str[0])) - 1u;
    if (chr_count > max_count) {
      chr_count = max_count;
    }

    for (size_t i = 0; i < chr_count; i++) {
      desc_str[1 + i] = str[i];
    }
    break;
  }
  }

  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * chr_count + 2u));
  return desc_str;
}
