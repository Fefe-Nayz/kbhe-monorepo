#include "bootloader_usb.h"

#include "tusb.h"
#include "updater_bootloader.h"
#include "updater_shared.h"
#include "usb_descriptors.h"

#include <stdbool.h>
#include <string.h>

#define BOOTLOADER_RX_QUEUE_DEPTH 4u

typedef struct {
  uint8_t data[UPDATER_PACKET_SIZE];
  uint16_t len;
} bootloader_rx_packet_t;

static bootloader_rx_packet_t s_rx_queue[BOOTLOADER_RX_QUEUE_DEPTH];
static volatile uint8_t s_rx_head = 0u;
static volatile uint8_t s_rx_tail = 0u;
static volatile uint8_t s_rx_count = 0u;
static uint8_t s_response_buffer[UPDATER_PACKET_SIZE];
static bool s_response_pending = false;
static volatile bool s_tx_in_flight = false;
static volatile bool s_tx_completed_since_task = false;

static bool bootloader_usb_send(const uint8_t *data, uint16_t len) {
  if (data == NULL || len != UPDATER_PACKET_SIZE || s_tx_in_flight ||
      !tud_hid_n_ready(HID_ITF_UPDATER)) {
    return false;
  }
  return tud_hid_n_report(HID_ITF_UPDATER, 0, data, len);
}

void bootloader_usb_init(void) {
  const tusb_rhport_init_t rhport_init = {.role = TUSB_ROLE_DEVICE,
                                          .speed = TUSB_SPEED_HIGH};

  s_rx_head = 0u;
  s_rx_tail = 0u;
  s_rx_count = 0u;
  s_response_pending = false;
  s_tx_in_flight = false;
  s_tx_completed_since_task = false;
  memset(s_rx_queue, 0, sizeof(s_rx_queue));
  memset(s_response_buffer, 0, sizeof(s_response_buffer));
  tusb_init(USB_RHPORT_HS, &rhport_init);
}

void bootloader_usb_task(void) {
  bootloader_rx_packet_t *packet = NULL;

  /* Let the main loop observe a BOOT response completion before another
   * queued command can mutate the updater state (for example ABORT). */
  if (s_tx_completed_since_task) {
    s_tx_completed_since_task = false;
    return;
  }
  if (s_tx_in_flight) {
    return;
  }
  if (s_response_pending) {
    if (bootloader_usb_send(s_response_buffer, sizeof(s_response_buffer))) {
      s_response_pending = false;
      s_tx_in_flight = true;
    }
    return;
  }
  if (s_rx_count == 0u) {
    return;
  }

  packet = &s_rx_queue[s_rx_tail];
  (void)updater_bootloader_process_packet(packet->data, packet->len,
                                           s_response_buffer);
  memset(packet, 0, sizeof(*packet));
  s_rx_tail = (uint8_t)((s_rx_tail + 1u) % BOOTLOADER_RX_QUEUE_DEPTH);
  s_rx_count--;
  s_response_pending = true;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  bootloader_rx_packet_t *slot = NULL;
  uint16_t copy_len = bufsize;
  (void)report_id;
  (void)report_type;

  if (instance != HID_ITF_UPDATER || buffer == NULL ||
      s_rx_count >= BOOTLOADER_RX_QUEUE_DEPTH) {
    return;
  }
  slot = &s_rx_queue[s_rx_head];
  memset(slot->data, 0, sizeof(slot->data));
  if (copy_len > sizeof(slot->data)) {
    copy_len = sizeof(slot->data);
  }
  memcpy(slot->data, buffer, copy_len);
  slot->len = bufsize;
  s_rx_head = (uint8_t)((s_rx_head + 1u) % BOOTLOADER_RX_QUEUE_DEPTH);
  s_rx_count++;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report,
                                uint16_t len) {
  (void)report;
  (void)len;

  if (instance == HID_ITF_UPDATER && s_tx_in_flight) {
    s_tx_in_flight = false;
    s_tx_completed_since_task = true;
    updater_bootloader_notify_response_sent();
  }
}
