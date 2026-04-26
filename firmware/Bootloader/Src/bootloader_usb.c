#include "bootloader_usb.h"

#include "tusb.h"
#include "updater_bootloader.h"
#include "updater_shared.h"
#include "usb_descriptors.h"

#include <string.h>

static uint8_t s_rx_buffer[UPDATER_PACKET_SIZE];
static uint8_t s_response_buffer[UPDATER_PACKET_SIZE];
static volatile bool s_rx_ready = false;
static volatile bool s_response_pending = false;

static bool bootloader_usb_send(const uint8_t *data, uint16_t len) {
  if (len > UPDATER_PACKET_SIZE) {
    len = UPDATER_PACKET_SIZE;
  }

  if (!tud_hid_n_ready(HID_ITF_UPDATER)) {
    return false;
  }

  return tud_hid_n_report(HID_ITF_UPDATER, 0, data, len);
}

void bootloader_usb_init(void) {
  const tusb_rhport_init_t rhport_init = {.role = TUSB_ROLE_DEVICE,
                                          .speed = TUSB_SPEED_HIGH};

  s_rx_ready = false;
  s_response_pending = false;
  memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
  memset(s_response_buffer, 0, sizeof(s_response_buffer));

  tusb_init(USB_RHPORT_HS, &rhport_init);
}

void bootloader_usb_task(void) {
  if (s_response_pending) {
    if (bootloader_usb_send(s_response_buffer, sizeof(s_response_buffer))) {
      s_response_pending = false;
    }
    return;
  }

  if (!s_rx_ready) {
    return;
  }

  s_rx_ready = false;
  updater_bootloader_process_packet(s_rx_buffer, s_response_buffer);
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
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  (void)report_id;
  (void)report_type;

  if (instance != HID_ITF_UPDATER) {
    return;
  }

  if (bufsize > sizeof(s_rx_buffer)) {
    bufsize = sizeof(s_rx_buffer);
  }

  memcpy(s_rx_buffer, buffer, bufsize);
  if (bufsize < sizeof(s_rx_buffer)) {
    memset(s_rx_buffer + bufsize, 0, sizeof(s_rx_buffer) - bufsize);
  }
  s_rx_ready = true;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report,
                                uint16_t len) {
  (void)report;
  (void)len;

  if (instance == HID_ITF_UPDATER) {
    updater_bootloader_notify_response_sent();
  }
}
