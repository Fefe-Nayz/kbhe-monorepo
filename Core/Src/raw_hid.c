
#include "raw_hid.h"
#include "hid_protocol.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <stdint.h>
#include <string.h>


// Raw HID interface instance number (Interface 1 in usb_descriptors.c)
#define RAW_HID_INSTANCE 1

// Buffer circulaire pour paquets reçus
static uint8_t rx_buffer[RAW_HID_BUFFER_SIZE];
static volatile uint16_t rx_size = 0;
static volatile bool rx_ready = false;
uint16_t rx_count = 0;
static volatile bool tx_state = false;

// Buffer pour envoi
static uint8_t tx_buffer[RAW_HID_BUFFER_SIZE];

// Response buffer for protocol
static uint8_t response_buffer[RAW_HID_BUFFER_SIZE];

void raw_hid_init(void) {
  rx_size = 0;
  rx_ready = false;
  rx_count = 0;
  tx_state = false;

  // Initialize the HID protocol handler (and settings)
  hid_protocol_init();
}

// À appeler dans tud_hid_set_report_cb pour stocker le paquet reçu
void raw_hid_on_receive(const uint8_t *data, uint16_t len) {
  if (len > RAW_HID_BUFFER_SIZE)
    len = RAW_HID_BUFFER_SIZE;
  memcpy((void *)rx_buffer, data, len);
  rx_size = len;
  rx_ready = true;
  rx_count++;
}

// Récupère un paquet reçu (copie dans buffer, retourne taille, 0 si rien)
uint16_t raw_hid_receive(uint8_t *buffer, uint16_t maxlen) {
  if (!rx_ready)
    return 0;
  uint16_t len = rx_size > maxlen ? maxlen : rx_size;
  memcpy(buffer, rx_buffer, len);
  rx_ready = false;
  rx_size = 0;
  return len;
}

// Envoie un paquet RAW HID (retourne true si envoyé)
// Uses tud_hid_n_report with instance 1 (Raw HID interface)
// report_id = 0 because TUD_HID_REPORT_DESC_GENERIC_INOUT doesn't use report
// IDs
bool raw_hid_send(const uint8_t *data, uint16_t len) {
  if (len > RAW_HID_BUFFER_SIZE)
    len = RAW_HID_BUFFER_SIZE;
  // Check if the Raw HID instance is ready (not instance 0 which is keyboard)
  if (!tud_hid_n_ready(RAW_HID_INSTANCE))
    return false;
  memcpy(tx_buffer, data, len);
  // Use instance 1 (Raw HID) with report_id = 0 (no report ID in generic
  // descriptor)
  return tud_hid_n_report(RAW_HID_INSTANCE, 0, tx_buffer, len);
}

// À appeler dans la boucle principale pour traiter les paquets reçus
void raw_hid_task(void) {
  uint8_t buffer[RAW_HID_BUFFER_SIZE];
  uint16_t len = raw_hid_receive(buffer, sizeof(buffer));

  if (len > 0) {
    // Process command through protocol handler
    if (hid_protocol_process(buffer, response_buffer)) {
      // Send response
      tx_state = raw_hid_send(response_buffer, RAW_HID_BUFFER_SIZE);
    }
  }
}
