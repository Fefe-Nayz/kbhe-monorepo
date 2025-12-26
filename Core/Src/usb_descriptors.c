/*
 * usb_descriptors.c
 * Implémentation des descripteurs USB pour clavier HID High Speed 8kHz
 * STM32F723VET6
 */

#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>
#include <stdio.h>

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,          // USB 2.0
    .bDeviceClass       = 0x00,             // Défini par interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,           // Version 1.0
    
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    
    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor - Clavier standard
//--------------------------------------------------------------------+
static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// Invoked when received GET HID REPORT DESCRIPTOR
uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

// Interface numbers
enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

/*
 * Macro TUD_HID_DESCRIPTOR personnalisée pour High Speed 8kHz
 * 
 * Format: TUD_HID_DESCRIPTOR(itf, stridx, protocol, report_desc_len, epin, epsize, interval)
 */
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    // bInterval = 1 pour 8kHz en High Speed (125µs)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD, 
                       sizeof(desc_hid_report), EPNUM_HID, HID_EP_SIZE, HID_POLL_INTERVAL_8KHZ)
};

//--------------------------------------------------------------------+
// High Speed Support - Device Qualifier & Other Speed Configuration
//--------------------------------------------------------------------+
#if TUD_OPT_HIGH_SPEED

// Device Qualifier Descriptor (requis pour High Speed)
static tusb_desc_device_qualifier_t const desc_device_qualifier = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00
};

// Invoked when received GET DEVICE QUALIFIER DESCRIPTOR request
uint8_t const* tud_descriptor_device_qualifier_cb(void) {
    return (uint8_t const*)&desc_device_qualifier;
}

// Other speed configuration (pour Full Speed quand connecté en HS capable)
static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN];

// Invoked when received GET OTHER SPEED CONFIGURATION DESCRIPTOR request
uint8_t const* tud_descriptor_other_speed_configuration_cb(uint8_t index) {
    (void)index;
    
    // Configuration identique mais avec type OTHER_SPEED_CONFIG
    memcpy(desc_other_speed_config, desc_configuration, CONFIG_TOTAL_LEN);
    desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    
    return desc_other_speed_config;
}

#endif // TUD_OPT_HIGH_SPEED

// Invoked when received GET CONFIGURATION DESCRIPTOR
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// Array of pointer to string descriptors
static char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: Language ID (English)
    "KBHE",                       // 1: Manufacturer
    "8kHz Keyboard",              // 2: Product
    NULL,                         // 3: Serial (will use chip ID)
};

// Buffer for string descriptor
static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;
    
    switch (index) {
        case STRID_LANGID:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;
            
        case STRID_SERIAL: {
            // Use STM32 Unique ID as serial number
            // UID is at address 0x1FF0F420 (96 bits = 12 bytes)
            volatile uint32_t* uid = (volatile uint32_t*)0x1FF0F420;
            char serial[25];
            snprintf(serial, sizeof(serial), "%08X%08X%08X", 
                     (unsigned int)uid[0], (unsigned int)uid[1], (unsigned int)uid[2]);
            chr_count = strlen(serial);
            if (chr_count > 32) chr_count = 32;
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = serial[i];
            }
            break;
        }
        
        default:
            // Check bounds
            if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
                return NULL;
            }
            
            const char* str = string_desc_arr[index];
            chr_count = strlen(str);
            if (chr_count > 32) chr_count = 32;
            
            // Convert ASCII to UTF-16
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }
    
    // First byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    
    return _desc_str;
}
