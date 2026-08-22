#include "usb_descriptors.h"
#include "hid/raw_hid.h"

#include "hid_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool endpoint_ready;
static uint8_t sent_report[RAW_HID_BUFFER_SIZE];
static uint32_t report_calls;
static uint32_t protocol_calls;
static uint32_t updater_notifications;
static bool complete_during_submit;

bool tud_hid_n_ready(uint8_t instance) {
  assert(instance == 1u);
  return endpoint_ready;
}

bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t len) {
  assert(instance == 1u);
  assert(report_id == 0u);
  assert(len == sizeof(sent_report));
  memcpy(sent_report, report, len);
  report_calls++;
  if (complete_during_submit) {
    raw_hid_on_report_complete();
  }
  return true;
}

void hid_protocol_init(void) {}

bool hid_protocol_process(const uint8_t *input, uint8_t *output) {
  protocol_calls++;
  memset(output, 0, RAW_HID_BUFFER_SIZE);
  output[0] = input[0];
  output[1] = HID_RESP_OK;
  return true;
}

bool hid_protocol_response_is_deferred(void) { return false; }

bool hid_protocol_poll_deferred_response(uint8_t *output) {
  (void)output;
  return false;
}

void updater_app_notify_response_sent(void) { updater_notifications++; }

static void reset_fixture(void) {
  endpoint_ready = true;
  memset(sent_report, 0, sizeof(sent_report));
  report_calls = 0u;
  protocol_calls = 0u;
  updater_notifications = 0u;
  complete_during_submit = false;
  raw_hid_init();
}

static void test_invalid_size_gets_deterministic_response(void) {
  const uint8_t short_request[] = {0x42u, 1u, 9u};

  reset_fixture();
  raw_hid_on_receive(short_request, sizeof(short_request));
  raw_hid_task(); /* dequeue and prepare */
  assert(report_calls == 0u);
  raw_hid_task(); /* submit when endpoint is ready */
  assert(report_calls == 1u);
  assert(sent_report[0] == 0x42u);
  assert(sent_report[1] == HID_RESP_INVALID_PARAM);
  assert(raw_hid_get_invalid_size_count() == 1u);
  assert(protocol_calls == 0u);

  raw_hid_on_report_complete();
  assert(updater_notifications == 1u);
  raw_hid_task(); /* completion grace turn */
  assert(report_calls == 1u);
}

static void test_response_retries_without_reexecuting_command(void) {
  uint8_t request[RAW_HID_BUFFER_SIZE] = {0};
  request[0] = 0x31u;

  reset_fixture();
  endpoint_ready = false;
  raw_hid_on_receive(request, sizeof(request));
  raw_hid_task();
  assert(protocol_calls == 1u);
  raw_hid_task();
  raw_hid_task();
  assert(protocol_calls == 1u);
  assert(report_calls == 0u);

  endpoint_ready = true;
  raw_hid_task();
  assert(report_calls == 1u);
  assert(protocol_calls == 1u);
}

static void test_queue_never_overwrites_older_requests(void) {
  uint8_t request[RAW_HID_BUFFER_SIZE] = {0};

  reset_fixture();
  for (uint8_t i = 0u; i < 5u; i++) {
    request[0] = i;
    raw_hid_on_receive(request, sizeof(request));
  }
  assert(raw_hid_get_rx_dropped_count() == 1u);

  raw_hid_task();
  raw_hid_task();
  assert(sent_report[0] == 0u); /* first queued packet was preserved */
}

static void test_spsc_counters_wrap_without_losing_capacity(void) {
  uint8_t request[RAW_HID_BUFFER_SIZE] = {0};
  uint8_t received[RAW_HID_BUFFER_SIZE] = {0};

  reset_fixture();
  for (uint16_t sequence = 0u; sequence < 300u; sequence++) {
    request[0] = (uint8_t)sequence;
    raw_hid_on_receive(request, sizeof(request));
    assert(raw_hid_receive(received, sizeof(received)) == sizeof(received));
    assert(received[0] == (uint8_t)sequence);
  }

  for (uint8_t i = 0u; i < 5u; i++) {
    request[0] = i;
    raw_hid_on_receive(request, sizeof(request));
  }
  assert(raw_hid_get_rx_dropped_count() == 1u);
}

static void test_direct_send_owns_endpoint_until_completion(void) {
  uint8_t response[RAW_HID_BUFFER_SIZE] = {0};

  reset_fixture();
  assert(raw_hid_send(response, sizeof(response)));
  assert(!raw_hid_send(response, sizeof(response)));
  raw_hid_on_report_complete();
  assert(raw_hid_send(response, sizeof(response)));
}

static void test_completion_during_submit_does_not_stick_endpoint(void) {
  uint8_t response[RAW_HID_BUFFER_SIZE] = {0};

  reset_fixture();
  complete_during_submit = true;
  assert(raw_hid_send(response, sizeof(response)));
  assert(updater_notifications == 1u);
  /* The completion callback already released ownership. */
  assert(raw_hid_send(response, sizeof(response)));
}

int main(void) {
  test_invalid_size_gets_deterministic_response();
  test_response_retries_without_reexecuting_command();
  test_queue_never_overwrites_older_requests();
  test_spsc_counters_wrap_without_losing_capacity();
  test_direct_send_owns_endpoint_until_completion();
  test_completion_during_submit_does_not_stick_endpoint();
  puts("raw_hid_test: ok");
  return 0;
}
