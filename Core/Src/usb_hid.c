/*
 * usb_hid.c
 * Implémentation des callbacks HID TinyUSB
 * et fonctions d'envoi de rapport clavier
 */

#include "tusb.h"
#include "usb_hid.h"
#include <string.h>

//--------------------------------------------------------------------+
// Variables internes
//--------------------------------------------------------------------+

// Rapport clavier courant
static hid_keyboard_report_t keyboard_report = {0};

// Flag pour indiquer qu'un rapport est prêt à être envoyé
static volatile bool report_pending = false;

//--------------------------------------------------------------------+
// API Publique - Fonctions d'envoi de rapport clavier
//--------------------------------------------------------------------+

bool usb_hid_keyboard_is_ready(void) {
    return tud_hid_ready();
}

bool usb_hid_keyboard_send_report(uint8_t modifier, const uint8_t keycodes[6]) {
    if (!tud_hid_ready()) {
        return false;
    }
    
    keyboard_report.modifier = modifier;
    keyboard_report.reserved = 0;
    
    if (keycodes != NULL) {
        memcpy(keyboard_report.keycode, keycodes, 6);
    } else {
        memset(keyboard_report.keycode, 0, 6);
    }
    
    // Pas de Report ID (0)
    return tud_hid_keyboard_report(0, keyboard_report.modifier, keyboard_report.keycode);
}

bool usb_hid_keyboard_press_key(uint8_t modifier, uint8_t keycode) {
    uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
    return usb_hid_keyboard_send_report(modifier, keycodes);
}

bool usb_hid_keyboard_release_all(void) {
    return usb_hid_keyboard_send_report(0, NULL);
}

void usb_hid_task(void) {
    // Cette fonction peut être appelée dans la boucle principale
    // pour gérer des envois de rapport périodiques si nécessaire
    
    // Pour l'instant, les rapports sont envoyés directement
    // via usb_hid_keyboard_send_report()
}

//--------------------------------------------------------------------+
// TinyUSB HID Callbacks (requis par TinyUSB)
//--------------------------------------------------------------------+

/*
 * Invoked when received GET_REPORT control request
 * Application must fill buffer report's content and return its length.
 * Return zero will cause the stack to STALL request
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type, uint8_t* buffer,
                                uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)reqlen;
    
    if (report_type == HID_REPORT_TYPE_INPUT) {
        // Retourner le rapport clavier courant
        memcpy(buffer, &keyboard_report, sizeof(keyboard_report));
        return sizeof(keyboard_report);
    }
    
    return 0;
}

/*
 * Invoked when received SET_REPORT control request or
 * received data on OUT endpoint (Report ID = 0, Type = OUTPUT)
 * 
 * Pour un clavier, cela correspond aux LEDs (Caps Lock, Num Lock, etc.)
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const* buffer,
                           uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    
    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // buffer[0] contient l'état des LEDs
        // Bit 0: Num Lock
        // Bit 1: Caps Lock
        // Bit 2: Scroll Lock
        // Bit 3: Compose
        // Bit 4: Kana
        
        if (bufsize >= 1) {
            uint8_t led_state = buffer[0];
            
            // Ici vous pouvez traiter l'état des LEDs
            // Par exemple, allumer/éteindre des LEDs physiques
            (void)led_state;
            
            // TODO: Implémenter le contrôle des LEDs si nécessaire
            // if (led_state & KEYBOARD_LED_CAPSLOCK) { ... }
        }
    }
}

/*
 * Invoked when sent REPORT successfully to host
 * Application can use this to send the next report
 */
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    (void)instance;
    (void)report;
    (void)len;
    
    // Rapport envoyé avec succès
    // Peut être utilisé pour déclencher l'envoi du prochain rapport
    report_pending = false;
}
