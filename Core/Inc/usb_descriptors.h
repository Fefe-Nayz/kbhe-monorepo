/*
 * usb_descriptors.h
 * Définitions des descripteurs USB pour clavier HID 8kHz
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>

//--------------------------------------------------------------------+
// USB Device Descriptor
//--------------------------------------------------------------------+

// VID/PID - Utiliser des valeurs de test
// IMPORTANT: Pour un produit commercial, obtenir un VID/PID officiel!
#define USB_VID   0x1209  // pid.codes VID (pour prototypage)
#define USB_PID   0x0001  // PID arbitraire pour test

// USB Version - USB 2.0 pour High Speed
#define USB_BCD   0x0200

//--------------------------------------------------------------------+
// HID Configuration
//--------------------------------------------------------------------+

// Endpoint number for HID IN
#define EPNUM_HID     0x81  // EP1 IN

// Endpoint size for High Speed HID
// High Speed permet jusqu'à 1024 bytes, mais 64 suffit pour un clavier
#define HID_EP_SIZE   64

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
#define HID_POLL_INTERVAL_8KHZ  1  // 125µs = 8kHz

//--------------------------------------------------------------------+
// String Descriptor Indices
//--------------------------------------------------------------------+
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

//--------------------------------------------------------------------+
// HID Report IDs (si plusieurs reports)
//--------------------------------------------------------------------+
// Pas de Report ID utilisé - rapport clavier simple
// Si besoin d'ajouter souris/consumer, définir les IDs ici

#endif /* USB_DESCRIPTORS_H_ */
