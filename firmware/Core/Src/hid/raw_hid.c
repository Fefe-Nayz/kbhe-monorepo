#include "hid/raw_hid.h"

#include "hid_protocol.h"
#include "tusb.h"
#include "updater_app.h"
#include "usb_descriptors.h"

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define RAW_HID_INSTANCE 1u
#define RAW_HID_RX_QUEUE_DEPTH 4u

typedef struct {
  uint8_t data[RAW_HID_BUFFER_SIZE];
  uint16_t received_len;
} raw_hid_rx_packet_t;

static raw_hid_rx_packet_t rx_queue[RAW_HID_RX_QUEUE_DEPTH];
/* Monotonic SPSC counters avoid a shared read/modify/write count between the
 * TinyUSB receive callback and the main-loop consumer. Release/acquire makes
 * a completed packet visible before its head is published. */
static atomic_uchar rx_head;
static atomic_uchar rx_tail;
static volatile uint32_t rx_dropped_count = 0u;
static volatile uint32_t rx_invalid_size_count = 0u;

static uint8_t tx_buffer[RAW_HID_BUFFER_SIZE];
static uint8_t response_buffer[RAW_HID_BUFFER_SIZE];
static bool response_pending = false;
static volatile bool tx_in_flight = false;
static volatile bool tx_retry_pending = false;
static volatile bool tx_completed_since_task = false;

void raw_hid_init(void) {
  memset(rx_queue, 0, sizeof(rx_queue));
  memset(tx_buffer, 0, sizeof(tx_buffer));
  memset(response_buffer, 0, sizeof(response_buffer));
  atomic_store_explicit(&rx_head, 0u, memory_order_relaxed);
  atomic_store_explicit(&rx_tail, 0u, memory_order_relaxed);
  rx_dropped_count = 0u;
  rx_invalid_size_count = 0u;
  response_pending = false;
  tx_in_flight = false;
  tx_retry_pending = false;
  tx_completed_since_task = false;
  hid_protocol_init();
}

void raw_hid_on_receive(const uint8_t *data, uint16_t len) {
  raw_hid_rx_packet_t *slot = NULL;
  uint16_t copy_len = len;
  uint8_t head = atomic_load_explicit(&rx_head, memory_order_relaxed);
  uint8_t tail = atomic_load_explicit(&rx_tail, memory_order_acquire);

  if (data == NULL) {
    rx_dropped_count++;
    return;
  }
  if ((uint8_t)(head - tail) >= RAW_HID_RX_QUEUE_DEPTH) {
    /* Preserve queued commands and their ordering.  Overwriting a request can
     * turn a retryable transport error into an unacknowledged side effect. */
    rx_dropped_count++;
    return;
  }

  slot = &rx_queue[head % RAW_HID_RX_QUEUE_DEPTH];
  memset(slot->data, 0, sizeof(slot->data));
  if (copy_len > sizeof(slot->data)) {
    copy_len = sizeof(slot->data);
  }
  memcpy(slot->data, data, copy_len);
  slot->received_len = len;
  atomic_store_explicit(&rx_head, (uint8_t)(head + 1u),
                        memory_order_release);
}

uint16_t raw_hid_receive(uint8_t *buffer, uint16_t maxlen) {
  raw_hid_rx_packet_t *slot = NULL;
  uint16_t copy_len = 0u;
  uint16_t received_len = 0u;
  uint8_t tail = atomic_load_explicit(&rx_tail, memory_order_relaxed);
  uint8_t head = atomic_load_explicit(&rx_head, memory_order_acquire);

  if (buffer == NULL || maxlen == 0u || tail == head) {
    return 0u;
  }
  slot = &rx_queue[tail % RAW_HID_RX_QUEUE_DEPTH];
  received_len = slot->received_len;
  copy_len = received_len;
  if (copy_len > sizeof(slot->data)) {
    copy_len = sizeof(slot->data);
  }
  if (copy_len > maxlen) {
    copy_len = maxlen;
  }
  memcpy(buffer, slot->data, copy_len);
  memset(slot, 0, sizeof(*slot));
  atomic_store_explicit(&rx_tail, (uint8_t)(tail + 1u),
                        memory_order_release);
  return received_len;
}

static bool raw_hid_submit_tx_buffer(void) {
  bool queued = false;

  if (tx_in_flight || !tud_hid_n_ready(RAW_HID_INSTANCE)) {
    return false;
  }
  /* Arm before handing the report to TinyUSB. A completion interrupt may run
   * as soon as the endpoint is started (including before this call returns),
   * and must never observe a false idle state for the submitted transfer. */
  tx_in_flight = true;
  queued = tud_hid_n_report(RAW_HID_INSTANCE, 0, tx_buffer,
                            sizeof(tx_buffer));
  if (!queued) {
    tx_in_flight = false;
  }
  return queued;
}

bool raw_hid_send(const uint8_t *data, uint16_t len) {
  if (data == NULL || len != RAW_HID_BUFFER_SIZE || tx_in_flight ||
      tx_retry_pending) {
    return false;
  }

  memcpy(tx_buffer, data, sizeof(tx_buffer));
  return raw_hid_submit_tx_buffer();
}

void raw_hid_on_report_complete(void) {
  if (!tx_in_flight) {
    return;
  }
  tx_in_flight = false;
  tx_retry_pending = false;
  tx_completed_since_task = true;
  updater_app_notify_response_sent();
}

void raw_hid_on_report_failed(void) {
  if (!tx_in_flight) {
    return;
  }

  /* The command may already have mutated state. Retain the exact response and
   * retry it without dequeuing or executing another request. */
  tx_in_flight = false;
  tx_retry_pending = true;
}

void raw_hid_on_umount(void) {
  if (tx_in_flight) {
    tx_in_flight = false;
    tx_retry_pending = true;
  }
  tx_completed_since_task = false;
}

static void raw_hid_prepare_invalid_size_response(const uint8_t *request,
                                                  uint16_t request_len) {
  hid_packet_t *response = (hid_packet_t *)response_buffer;
  memset(response_buffer, 0, sizeof(response_buffer));
  response->command_id = request_len > 0u ? request[0] : (uint8_t)CMD_UNKNOWN;
  response->status_or_len = HID_RESP_INVALID_PARAM;
}

void raw_hid_task(void) {
  uint8_t request[RAW_HID_BUFFER_SIZE] = {0};
  uint16_t request_len = 0u;

  /* Give updater_app_task one main-loop turn to execute a reboot/re-enumeration
   * associated with the response that just completed. */
  if (tx_completed_since_task) {
    tx_completed_since_task = false;
    return;
  }

  /* Stop-and-wait: a command is not dequeued until the previous response has
   * been accepted by TinyUSB and completed by the endpoint. */
  if (tx_in_flight) {
    return;
  }
  if (tx_retry_pending) {
    tx_retry_pending = false;
    if (!raw_hid_submit_tx_buffer()) {
      tx_retry_pending = true;
    }
    return;
  }
  if (response_pending) {
    if (raw_hid_send(response_buffer, sizeof(response_buffer))) {
      response_pending = false;
    }
    return;
  }

  /* Mutating commands that require durable Flash publication retain the
   * stop-and-wait slot until their budgeted persistence state machine ends. */
  if (hid_protocol_response_is_deferred()) {
    if (hid_protocol_poll_deferred_response(response_buffer)) {
      response_pending = true;
    }
    return;
  }

  request_len = raw_hid_receive(request, sizeof(request));
  if (request_len == 0u) {
    return;
  }
  if (request_len != RAW_HID_BUFFER_SIZE) {
    rx_invalid_size_count++;
    raw_hid_prepare_invalid_size_response(request, request_len);
    response_pending = true;
    return;
  }

  memset(response_buffer, 0, sizeof(response_buffer));
  if (hid_protocol_process(request, response_buffer)) {
    response_pending = true;
  }
}

uint32_t raw_hid_get_rx_dropped_count(void) { return rx_dropped_count; }

uint32_t raw_hid_get_invalid_size_count(void) {
  return rx_invalid_size_count;
}
