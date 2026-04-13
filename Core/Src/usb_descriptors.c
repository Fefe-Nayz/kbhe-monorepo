/*
 * usb_descriptors.c
 * Implémentation des descripteurs USB pour clavier HID High Speed 8kHz
 * STM32F723VET6
 */

#include "usb_descriptors.h"
#include "settings.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,    // Overridden dynamically for XInput mode
    .bDeviceClass = 0x00, // Défini par interface
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0104, // Overridden dynamically so Windows caches HID/XInput separately

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 0x01};

static bool usb_descriptors_use_xinput(void);
static uint16_t usb_descriptors_bcd_usb(void);
static uint16_t usb_descriptors_bcd_device(void);

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const *tud_descriptor_device_cb(void) {
  desc_device.bcdUSB = usb_descriptors_bcd_usb();
  desc_device.bcdDevice = usb_descriptors_bcd_device();
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
// HID Report Descriptor - Classic pad layout
// Mirrors the common XUSB HID shape:
// - left stick  : X/Y   (16-bit, centered)
// - right stick : Rx/Ry (16-bit, centered)
// - triggers    : Z/Rz  (8-bit, unipolar)
// - buttons     : A/B/X/Y/LB/RB/Back/Start/L3/R3
// - dpad        : Hat switch
//--------------------------------------------------------------------+
static uint8_t const desc_gamepad_report[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)

    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x15, 0x00,       //     Logical Minimum (0)
    0x26, 0xFF, 0xFF, //     Logical Maximum (65535)
    0x35, 0x00,       //     Physical Minimum (0)
    0x46, 0xFF, 0xFF, //     Physical Maximum (65535)
    0x95, 0x02,       //     Report Count (2)
    0x75, 0x10,       //     Report Size (16)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0xC0,             //   End Collection

    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x33,       //     Usage (Rx)
    0x09, 0x34,       //     Usage (Ry)
    0x15, 0x00,       //     Logical Minimum (0)
    0x26, 0xFF, 0xFF, //     Logical Maximum (65535)
    0x35, 0x00,       //     Physical Minimum (0)
    0x46, 0xFF, 0xFF, //     Physical Maximum (65535)
    0x95, 0x02,       //     Report Count (2)
    0x75, 0x10,       //     Report Size (16)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0xC0,             //   End Collection

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x32,       //   Usage (Z)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x02,       //   Input (Data,Var,Abs)

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x35,       //   Usage (Rz)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x02,       //   Input (Data,Var,Abs)

    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (Button 1)
    0x29, 0x0A,       //   Usage Maximum (Button 10)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x95, 0x0A,       //   Report Count (10)
    0x75, 0x01,       //   Report Size (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Usage (Hat switch)
    0x15, 0x01,       //   Logical Minimum (1)
    0x25, 0x08,       //   Logical Maximum (8)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x10, //   Physical Maximum (4155)
    0x66, 0x0E, 0x00, //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data,Var,Abs,Null)

    0x75, 0x02,       //   Report Size (2)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x03,       //   Input (Const,Var,Abs)

    0x75, 0x08,       //   Report Size (8)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x03,       //   Input (Const,Var,Abs)
    0xC0              // End Collection
};

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
  ITF_NUM_NKRO,
  ITF_NUM_CONSUMER,
  ITF_NUM_MOUSE,
  ITF_NUM_GAMEPAD_API,
  ITF_NUM_TOTAL_MAX
};

#define ITF_NUM_XINPUT ITF_NUM_GAMEPAD_API
#define ITF_NUM_GAMEPAD ITF_NUM_GAMEPAD_API

// Invoked when received GET HID REPORT DESCRIPTOR
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  switch (instance) {
  case ITF_NUM_HID:
    return desc_hid_report;
  case ITF_NUM_RAW_HID:
    return desc_raw_hid_report;
  case ITF_NUM_NKRO:
    return desc_nkro_report;
  case ITF_NUM_CONSUMER:
    return desc_consumer_report;
  case ITF_NUM_MOUSE:
    return desc_mouse_report;
  case ITF_NUM_GAMEPAD:
    return desc_gamepad_report;
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
#define CONFIG_TOTAL_LEN_BASE                                                  \
  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN +           \
   TUD_HID_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN)

#define CONFIG_TOTAL_LEN_MAX (CONFIG_TOTAL_LEN_BASE + XINPUT_DESC_LEN)

#define XINPUT_DESCRIPTOR(itfnum, stridx, epout, epin, ep_interval)            \
  9, TUSB_DESC_INTERFACE, itfnum, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC,            \
      XINPUT_SUBCLASS_DEFAULT, XINPUT_PROTOCOL_DEFAULT, stridx,                \
      16, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0110), 0x01, 0x24, epin, 0x14,    \
      0x03, 0x00, 0x03, 0x13, epout, 0x00, 0x03, 0x00,                         \
      7, TUSB_DESC_ENDPOINT, epin, TUSB_XFER_INTERRUPT,                         \
      U16_TO_U8S_LE(XINPUT_EP_SIZE), ep_interval,                               \
      7, TUSB_DESC_ENDPOINT, epout, TUSB_XFER_INTERRUPT,                        \
      U16_TO_U8S_LE(XINPUT_EP_SIZE), ep_interval

static uint8_t desc_configuration[CONFIG_TOTAL_LEN_MAX];

static bool usb_descriptors_use_xinput(void) {
  return settings_get_gamepad_api_mode() == GAMEPAD_API_XINPUT;
}

static uint16_t usb_descriptors_bcd_usb(void) {
  return usb_descriptors_use_xinput() ? 0x0210u : 0x0200u;
}

static uint16_t usb_descriptors_bcd_device(void) {
  return usb_descriptors_use_xinput() ? 0x0105u : 0x0104u;
}

static void generate_desc_configuration(uint8_t *dst) {
  uint8_t mandatory_offset = 0u;
  uint16_t total_length = CONFIG_TOTAL_LEN_BASE;
  const uint8_t mandatory_desc[] = {
      TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_MAX, 0, 0,
                            TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

      // Interface 0: Keyboard HID (Boot-compatible 6KRO)
      TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_KEYBOARD_BOOT,
             HID_ITF_PROTOCOL_KEYBOARD,
                         sizeof(desc_hid_report), EPNUM_HID, HID_EP_SIZE,
                         HID_POLL_INTERVAL_8KHZ),

      // Interface 1: Raw HID IN/OUT control channel
      TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_RAW_HID, STRID_RAW_HID,
                               HID_ITF_PROTOCOL_NONE,
                               sizeof(desc_raw_hid_report), EPNUM_RAW_HID_OUT,
                               0x80 | EPNUM_RAW_HID_IN, HID_EP_SIZE,
                               RAW_HID_POLL_INTERVAL),

      // Interface 2: NKRO keyboard HID
      TUD_HID_DESCRIPTOR(ITF_NUM_NKRO, STRID_NKRO, HID_ITF_PROTOCOL_KEYBOARD,
                         sizeof(desc_nkro_report), EPNUM_NKRO, HID_EP_SIZE,
                         HID_POLL_INTERVAL_8KHZ),

      // Interface 3: Consumer Control HID
      TUD_HID_DESCRIPTOR(ITF_NUM_CONSUMER, STRID_CONSUMER,
                         HID_ITF_PROTOCOL_NONE, sizeof(desc_consumer_report),
                         EPNUM_CONSUMER, HID_EP_SIZE, HID_POLL_INTERVAL_8KHZ),

      // Interface 4: Mouse HID
      TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, STRID_MOUSE, HID_ITF_PROTOCOL_MOUSE,
                         sizeof(desc_mouse_report), EPNUM_MOUSE, HID_EP_SIZE,
                         HID_POLL_INTERVAL_8KHZ),
  };
  const uint8_t xinput_desc[] = {
      XINPUT_DESCRIPTOR(ITF_NUM_XINPUT, STRID_XINPUT, EPNUM_XINPUT_OUT,
                        EPNUM_XINPUT_IN, XINPUT_POLL_INTERVAL)};
  const uint8_t gamepad_desc[] = {
      TUD_HID_DESCRIPTOR(ITF_NUM_GAMEPAD, STRID_GAMEPAD, HID_ITF_PROTOCOL_NONE,
                         sizeof(desc_gamepad_report), EPNUM_GAMEPAD, HID_EP_SIZE,
                         GAMEPAD_POLL_INTERVAL)};

  memcpy(dst, mandatory_desc, sizeof(mandatory_desc));
  mandatory_offset = (uint8_t)sizeof(mandatory_desc);

  if (usb_descriptors_use_xinput()) {
    memcpy(&dst[mandatory_offset], xinput_desc, sizeof(xinput_desc));
    total_length = (uint16_t)(CONFIG_TOTAL_LEN_BASE + sizeof(xinput_desc));
  } else {
    memcpy(&dst[mandatory_offset], gamepad_desc, sizeof(gamepad_desc));
    total_length = (uint16_t)(CONFIG_TOTAL_LEN_BASE + sizeof(gamepad_desc));
  }

  dst[2] = (uint8_t)(total_length & 0xFFu);
  dst[3] = (uint8_t)(total_length >> 8);
}

//--------------------------------------------------------------------+
// High Speed Support - Device Qualifier & Other Speed Configuration
//--------------------------------------------------------------------+
#if TUD_OPT_HIGH_SPEED

// Device Qualifier Descriptor (requis pour High Speed)
static tusb_desc_device_qualifier_t desc_device_qualifier = {
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
  desc_device_qualifier.bcdUSB = usb_descriptors_bcd_usb();
  return (uint8_t const *)&desc_device_qualifier;
}

// Other speed configuration (pour Full Speed quand connecté en HS capable)
static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN_MAX];

// Invoked when received GET OTHER SPEED CONFIGURATION DESCRIPTOR request
uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
  (void)index;

  // Configuration identique mais avec type OTHER_SPEED_CONFIG
  generate_desc_configuration(desc_other_speed_config);
  desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;

  return desc_other_speed_config;
}

#endif // TUD_OPT_HIGH_SPEED

// Invoked when received GET CONFIGURATION DESCRIPTOR
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  generate_desc_configuration(desc_configuration);
  return desc_configuration;
}

#define MS_OS_20_VENDOR_CODE 0x01
#define MS_OS_20_DESC_LEN 178

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, MS_OS_20_VENDOR_CODE),
};
static uint8_t const desc_bos_plain[] = {
    TUD_BOS_DESCRIPTOR(TUD_BOS_DESC_LEN, 0),
};

static uint8_t const desc_ms_os_20[] = {
    // Set header
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    // Configuration subset header
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0x00, 0x00, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x000A),

    // Function subset header (XInput interface)
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_NUM_XINPUT, 0x00, U16_TO_U8S_LE(0x00A0),

    // Compatible ID descriptor: XUSB20
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'X', 'U', 'S', 'B', '2', '0', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // Registry property descriptor: DeviceInterfaceGUIDs (REG_MULTI_SZ)
    U16_TO_U8S_LE(0x0084), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    U16_TO_U8S_LE('D'), U16_TO_U8S_LE('e'), U16_TO_U8S_LE('v'),
    U16_TO_U8S_LE('i'), U16_TO_U8S_LE('c'), U16_TO_U8S_LE('e'),
    U16_TO_U8S_LE('I'), U16_TO_U8S_LE('n'), U16_TO_U8S_LE('t'),
    U16_TO_U8S_LE('e'), U16_TO_U8S_LE('r'), U16_TO_U8S_LE('f'),
    U16_TO_U8S_LE('a'), U16_TO_U8S_LE('c'), U16_TO_U8S_LE('e'),
    U16_TO_U8S_LE('G'), U16_TO_U8S_LE('U'), U16_TO_U8S_LE('I'),
    U16_TO_U8S_LE('D'), U16_TO_U8S_LE('s'), U16_TO_U8S_LE('\0'),
    U16_TO_U8S_LE(0x0050),
    U16_TO_U8S_LE('{'), U16_TO_U8S_LE('8'), U16_TO_U8S_LE('E'),
    U16_TO_U8S_LE('8'), U16_TO_U8S_LE('0'), U16_TO_U8S_LE('F'),
    U16_TO_U8S_LE('9'), U16_TO_U8S_LE('E'), U16_TO_U8S_LE('D'),
    U16_TO_U8S_LE('-'), U16_TO_U8S_LE('6'), U16_TO_U8S_LE('D'),
    U16_TO_U8S_LE('4'), U16_TO_U8S_LE('C'), U16_TO_U8S_LE('-'),
    U16_TO_U8S_LE('4'), U16_TO_U8S_LE('E'), U16_TO_U8S_LE('E'),
    U16_TO_U8S_LE('5'), U16_TO_U8S_LE('-'), U16_TO_U8S_LE('9'),
    U16_TO_U8S_LE('B'), U16_TO_U8S_LE('8'), U16_TO_U8S_LE('9'),
    U16_TO_U8S_LE('-'), U16_TO_U8S_LE('7'), U16_TO_U8S_LE('C'),
    U16_TO_U8S_LE('C'), U16_TO_U8S_LE('2'), U16_TO_U8S_LE('D'),
    U16_TO_U8S_LE('C'), U16_TO_U8S_LE('A'), U16_TO_U8S_LE('6'),
    U16_TO_U8S_LE('D'), U16_TO_U8S_LE('4'), U16_TO_U8S_LE('A'),
    U16_TO_U8S_LE('4'), U16_TO_U8S_LE('}'), U16_TO_U8S_LE('\0'),
    U16_TO_U8S_LE('\0'),
};

_Static_assert(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN,
               "Invalid Microsoft OS 2.0 descriptor size");

uint8_t const *tud_descriptor_bos_cb(void) {
  return usb_descriptors_use_xinput() ? desc_bos : desc_bos_plain;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
  if (!usb_descriptors_use_xinput()) {
    return false;
  }

  if (stage != CONTROL_STAGE_SETUP) {
    return true;
  }

  if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
      request->bRequest == MS_OS_20_VENDOR_CODE && request->wIndex == 0x07) {
    return tud_control_xfer(rhport, request, (void *)desc_ms_os_20,
                            MS_OS_20_DESC_LEN);
  }

  return false;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// 1. Définition des chaînes statiques
// L'ordre DOIT correspondre à votre enum (0, 1, 2, 3...)
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: LangID (English US)
  "KBHE",                     // 1: Manufacturer
  "75HE Keyboard",            // 2: Product
    NULL,                       // 3: Serial (Géré dynamiquement)
  "75HE Keyboard (Boot)",     // 4: Interface Boot keyboard (6KRO)
  "75HE Control",             // 5: Interface Raw HID
  "75HE Gamepad",             // 6: Interface Gamepad (Hall Effect)
  "75HE Keyboard",            // 7: Interface NKRO Keyboard
  "75HE Consumer Control",    // 8: Consumer Control interface
  "75HE Mouse",               // 9: Mouse interface
  "75HE Gamepad"              // 10: XInput-compatible interface
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
