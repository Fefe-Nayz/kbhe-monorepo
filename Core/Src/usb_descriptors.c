/*
 * usb_descriptors.c
 * Implémentation des descripteurs USB pour clavier HID High Speed 8kHz
 * STM32F723VET6
 */

#include "usb_descriptors.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,    // USB 2.0
    .bDeviceClass = 0x00, // Défini par interface
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100, // Version 1.0

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 0x01};

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor - Clavier standard (6KRO)
//--------------------------------------------------------------------+
static uint8_t const desc_hid_report[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

//--------------------------------------------------------------------+
// HID Report Descriptor - NKRO Keyboard (bitmap-based)
// Supports full N-key rollover via bitmap
//--------------------------------------------------------------------+
static uint8_t const desc_nkro_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_KEYBOARD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),

    // Modifier keys (8 bits)
    HID_USAGE_PAGE(HID_USAGE_PAGE_KEYBOARD),
    HID_USAGE_MIN(224), // Left Control
    HID_USAGE_MAX(231), // Right GUI
    HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(1), HID_REPORT_COUNT(8),
    HID_REPORT_SIZE(1), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    // LED output (5 bits)
    HID_USAGE_PAGE(HID_USAGE_PAGE_LED),
    HID_USAGE_MIN(1), // Num Lock
    HID_USAGE_MAX(5), // Kana
    HID_REPORT_COUNT(5), HID_REPORT_SIZE(1),
    HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    // Padding for LED byte alignment
    HID_REPORT_COUNT(1), HID_REPORT_SIZE(3), HID_OUTPUT(HID_CONSTANT),

    // Keyboard bitmap (keys 0-127 = 16 bytes)
    HID_USAGE_PAGE(HID_USAGE_PAGE_KEYBOARD), HID_USAGE_MIN(0),
    HID_USAGE_MAX(127), // Supports keys 0-127
    HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(1), HID_REPORT_COUNT(128),
    HID_REPORT_SIZE(1), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    HID_COLLECTION_END};

static uint8_t const desc_raw_hid_report[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(64)};

//--------------------------------------------------------------------+
// HID Report Descriptor - Gamepad v1
// 32 buttons + 4 centered stick axes + 2 analog triggers
//--------------------------------------------------------------------+
static uint8_t const desc_gamepad_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
    HID_USAGE_MIN(1),
    HID_USAGE_MAX(32),
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_COUNT(32),
    HID_REPORT_SIZE(1),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_X),
    HID_USAGE(HID_USAGE_DESKTOP_Y),
    HID_USAGE(HID_USAGE_DESKTOP_RX),
    HID_USAGE(HID_USAGE_DESKTOP_RY),
    HID_LOGICAL_MIN_N(-127, 1),
    HID_LOGICAL_MAX(127),
    HID_REPORT_COUNT(4),
    HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    HID_USAGE(HID_USAGE_DESKTOP_Z),
    HID_USAGE(HID_USAGE_DESKTOP_RZ),
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX_N(255, 2),
    HID_REPORT_COUNT(2),
    HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END};

// Consumer control interface: media keys / volume
static uint8_t const desc_consumer_report[] = {
    TUD_HID_REPORT_DESC_CONSUMER()};

static uint8_t const desc_mouse_report[] = {TUD_HID_REPORT_DESC_MOUSE()};

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

// Interface numbers
enum {
  ITF_NUM_HID,
  ITF_NUM_RAW_HID,
  ITF_NUM_GAMEPAD,
  ITF_NUM_NKRO,
  ITF_NUM_CONSUMER,
  ITF_NUM_MOUSE,
  ITF_NUM_TOTAL
};

// Invoked when received GET HID REPORT DESCRIPTOR
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  switch (instance) {
  case ITF_NUM_HID:
    return desc_hid_report;
  case ITF_NUM_RAW_HID:
    return desc_raw_hid_report;
  case ITF_NUM_GAMEPAD:
    return desc_gamepad_report;
  case ITF_NUM_NKRO:
    return desc_nkro_report;
  case ITF_NUM_CONSUMER:
    return desc_consumer_report;
  case ITF_NUM_MOUSE:
    return desc_mouse_report;
  default:
    return NULL;
  }
}

/*
 * Macro TUD_HID_DESCRIPTOR personnalisée pour High Speed 8kHz
 *
 * Format: TUD_HID_DESCRIPTOR(itf, stridx, protocol, report_desc_len, epin,
 * epsize, interval)
 */
#define CONFIG_TOTAL_LEN                                                       \
  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN +           \
   TUD_HID_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN +                    \
   TUD_HID_DESC_LEN)

static uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute,
    // power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface 0: Keyboard HID (6KRO)
    // bInterval = 1 pour 8kHz en High Speed (125µs)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report), EPNUM_HID, HID_EP_SIZE,
                       HID_POLL_INTERVAL_8KHZ),

    // Interface 1: Raw HID IN/OUT
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_RAW_HID, STRID_RAW_HID,
                             HID_ITF_PROTOCOL_NONE, sizeof(desc_raw_hid_report),
                             EPNUM_RAW_HID_OUT, 0x80 | EPNUM_RAW_HID_IN,
                             HID_EP_SIZE, RAW_HID_POLL_INTERVAL),

    // Interface 2: Gamepad HID
    TUD_HID_DESCRIPTOR(ITF_NUM_GAMEPAD, STRID_GAMEPAD, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_gamepad_report), EPNUM_GAMEPAD, HID_EP_SIZE,
                       GAMEPAD_POLL_INTERVAL),

    // Interface 3: NKRO Keyboard HID
    // NKRO uses a 17-byte report: 1 modifier + 16 bitmap bytes (128 keys)
    TUD_HID_DESCRIPTOR(ITF_NUM_NKRO, STRID_NKRO, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_nkro_report), EPNUM_NKRO, HID_EP_SIZE,
                       HID_POLL_INTERVAL_8KHZ),

    // Interface 4: Consumer Control HID (volume / media keys)
    TUD_HID_DESCRIPTOR(ITF_NUM_CONSUMER, STRID_CONSUMER, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_consumer_report), EPNUM_CONSUMER, HID_EP_SIZE,
                       HID_POLL_INTERVAL_8KHZ),

    // Interface 5: Mouse HID (buttons + wheel)
    TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, STRID_MOUSE, HID_ITF_PROTOCOL_MOUSE,
                       sizeof(desc_mouse_report), EPNUM_MOUSE, HID_EP_SIZE,
                       HID_POLL_INTERVAL_8KHZ)};

//--------------------------------------------------------------------+
// High Speed Support - Device Qualifier & Other Speed Configuration
//--------------------------------------------------------------------+
#if TUD_OPT_HIGH_SPEED

// Device Qualifier Descriptor (requis pour High Speed)
static tusb_desc_device_qualifier_t const desc_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0x00};

// Invoked when received GET DEVICE QUALIFIER DESCRIPTOR request
uint8_t const *tud_descriptor_device_qualifier_cb(void) {
  return (uint8_t const *)&desc_device_qualifier;
}

// Other speed configuration (pour Full Speed quand connecté en HS capable)
static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN];

// Invoked when received GET OTHER SPEED CONFIGURATION DESCRIPTOR request
uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
  (void)index;

  // Configuration identique mais avec type OTHER_SPEED_CONFIG
  memcpy(desc_other_speed_config, desc_configuration, CONFIG_TOTAL_LEN);
  desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;

  return desc_other_speed_config;
}

#endif // TUD_OPT_HIGH_SPEED

// Invoked when received GET CONFIGURATION DESCRIPTOR
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// 1. Définition des chaînes statiques
// L'ordre DOIT correspondre à votre enum (0, 1, 2, 3...)
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: LangID (English US)
    "KBHE",                     // 1: Manufacturer
    "8kHz Keyboard",            // 2: Product
    NULL,                       // 3: Serial (Géré dynamiquement)
    "Raw HID",                  // 4: Interface Raw HID
    "HE Gamepad",               // 5: Interface Gamepad (Hall Effect)
    "NKRO Keyboard",            // 6: Interface NKRO Keyboard
    "Consumer Control",         // 7: Consumer Control interface
    "Mouse"                     // 8: Mouse interface
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long
// enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  size_t chr_count;

  switch (index) {
  case STRID_LANGID:
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
    break;

  case STRID_SERIAL: {
    // Use STM32 Unique ID as serial number
    // UID base address for STM32F72x/F73x is 0x1FF07A10 (96 bits = 12 bytes)
    static const char hex_chars[] = "0123456789ABCDEF";
    volatile uint32_t *uid = (volatile uint32_t *)0x1FF07A10;

    // Convert 3x 32-bit words to 24 hex characters
    chr_count = 24;
    for (uint8_t w = 0; w < 3; w++) {
      uint32_t val = uid[w];
      for (int8_t n = 7; n >= 0; n--) {
        _desc_str[1 + w * 8 + (7 - n)] = hex_chars[(val >> (n * 4)) & 0x0F];
      }
    }
    break;
  } break;

  default:
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

    if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
      return NULL;

    const char *str = string_desc_arr[index];

    // Cap at max char
    chr_count = strlen(str);
    size_t const max_count =
        sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
    if (chr_count > max_count)
      chr_count = max_count;

    // Convert ASCII string into UTF-16
    for (size_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }
    break;
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}
