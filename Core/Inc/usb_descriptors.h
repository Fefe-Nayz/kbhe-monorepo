/*
 * usb_descriptors.h
 * Définitions des descripteurs USB pour clavier HID 8kHz
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>
#include "updater_shared.h"

//--------------------------------------------------------------------+
// USB Device Descriptor
//--------------------------------------------------------------------+

// VID/PID - Utiliser des valeurs de test
// IMPORTANT: Pour un produit commercial, obtenir un VID/PID officiel!
#define USB_VID KBHE_USB_VID
#define USB_PID KBHE_APP_USB_PID

#define USB_RHPORT_HS 1u

// USB Version - USB 2.0 pour High Speed
#define USB_BCD 0x0200

//--------------------------------------------------------------------+
// HID Configuration
//--------------------------------------------------------------------+

// Endpoint number for HID IN (Keyboard)
#define EPNUM_HID 0x81 // EP1 IN
// Endpoint for RAW HID IN/OUT
#define EPNUM_RAW_HID_IN 0x02  // EP2 IN (will be OR'd with 0x80)
#define EPNUM_RAW_HID_OUT 0x02 // EP2 OUT
// Endpoint for Gamepad IN
#define EPNUM_GAMEPAD 0x83 // EP3 IN
// Endpoint for NKRO Keyboard IN
#define EPNUM_NKRO 0x84 // EP4 IN

// Endpoint size for High Speed HID
// High Speed permet jusqu'à 1024 bytes, mais 64 suffit pour un clavier
#define HID_EP_SIZE 64

/*
 * bInterval pour USB High Speed HID à 8kHz
 *
 * En High Speed, bInterval est en unités de 125µs (microframes)
 * avec exposant: interval = 2^(bInterval-1) * 125µs
 *
 * Pour 8kHz = 125µs par rapport = 1 microframe
 * bInterval = 1 → 2^(1-1) = 2^0 = 1 microframe = 125µs = 8000 Hz ✓
 *
 * Valeurs possibles:
 *   bInterval=1 → 125µs   = 8000 Hz (8kHz)
 *   bInterval=2 → 250µs   = 4000 Hz
 *   bInterval=3 → 500µs   = 2000 Hz
 *   bInterval=4 → 1000µs  = 1000 Hz (1kHz)
 */
#define HID_POLL_INTERVAL_8KHZ 1 // 125µs = 8kHz
#define RAW_HID_POLL_INTERVAL 4  // 1000µs = 1kHz
#define GAMEPAD_POLL_INTERVAL                                                  \
  1 // 125µs = 8kHz (same as keyboard for fast response)

//--------------------------------------------------------------------+
// String Descriptor Indices
//--------------------------------------------------------------------+
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_RAW_HID,
  STRID_GAMEPAD,
  STRID_NKRO,
};

//--------------------------------------------------------------------+
// HID Interface Instance Numbers
//--------------------------------------------------------------------+
enum {
  HID_ITF_KEYBOARD = 0,
  HID_ITF_RAW_HID = 1,
  HID_ITF_GAMEPAD = 2,
  HID_ITF_NKRO = 3,
};

//--------------------------------------------------------------------+
// HID Report IDs (si plusieurs reports)
//--------------------------------------------------------------------+
// Pas de Report ID utilisé - rapport clavier simple
// Si besoin d'ajouter souris/consumer, définir les IDs ici

#endif /* USB_DESCRIPTORS_H_ */
