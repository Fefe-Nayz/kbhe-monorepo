
#include "raw_hid.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <stdint.h>
#include <string.h>

// Buffer circulaire pour paquets reçus
static uint8_t rx_buffer[RAW_HID_BUFFER_SIZE];
static volatile uint16_t rx_size = 0;
static volatile bool rx_ready = false;
uint16_t rx_count = 0;
static volatile bool tx_state = false;

// Buffer pour envoi (optionnel)
static uint8_t tx_buffer[RAW_HID_BUFFER_SIZE];

void raw_hid_init(void) {
    rx_size = 0;
    rx_ready = false;
    rx_count = 0;
    tx_state = false;
}

// À appeler dans tud_hid_set_report_cb pour stocker le paquet reçu
void raw_hid_on_receive(const uint8_t *data, uint16_t len) {
    if (len > RAW_HID_BUFFER_SIZE) len = RAW_HID_BUFFER_SIZE;
    memcpy((void*)rx_buffer, data, len);
    rx_size = len;
    rx_ready = true;
    rx_count++;
}

// Récupère un paquet reçu (copie dans buffer, retourne taille, 0 si rien)
uint16_t raw_hid_receive(uint8_t *buffer, uint16_t maxlen) {
    if (!rx_ready) return 0;
    uint16_t len = rx_size > maxlen ? maxlen : rx_size;
    memcpy(buffer, rx_buffer, len);
    rx_ready = false;
    rx_size = 0;
    return len;
}

// Envoie un paquet RAW HID (retourne true si envoyé)
bool raw_hid_send(const uint8_t *data, uint16_t len) {
    if (len > RAW_HID_BUFFER_SIZE) len = RAW_HID_BUFFER_SIZE;
    if (!tud_hid_ready()) return false;
    memcpy(tx_buffer, data, len);
    return tud_hid_report(1, tx_buffer, len) == true;
}

// À appeler dans la boucle principale pour traiter les paquets reçus
void raw_hid_task(void) {
    // Echo automatique : renvoie le buffer reçu en RAW HID
    uint8_t buffer[RAW_HID_BUFFER_SIZE];
    uint16_t len = raw_hid_receive(buffer, sizeof(buffer));
    if (len > 0) {
        tx_state = raw_hid_send(buffer, len);
    }
}
