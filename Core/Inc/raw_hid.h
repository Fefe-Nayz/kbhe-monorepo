/*
 * raw_hid.h
 * API pour RAW HID (réception, gestion, envoi)
 */

#ifndef RAW_HID_H_
#define RAW_HID_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_HID_BUFFER_SIZE  HID_EP_SIZE

// Initialise le module RAW HID (optionnel)
void raw_hid_init(void);

// Fonction à appeler dans la boucle principale pour traiter les paquets reçus
void raw_hid_task(void);

// Récupère un paquet reçu (copie dans buffer, retourne taille, 0 si rien)
uint16_t raw_hid_receive(uint8_t *buffer, uint16_t maxlen);

// Envoie un paquet RAW HID (retourne true si envoyé)
bool raw_hid_send(const uint8_t *data, uint16_t len);

// Callback interne pour TinyUSB (à appeler dans tud_hid_set_report_cb)
void raw_hid_on_receive(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* RAW_HID_H_ */
