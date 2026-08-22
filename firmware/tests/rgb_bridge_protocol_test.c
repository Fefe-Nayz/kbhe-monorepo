#include "hid_protocol.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
  assert(CMD_GET_RGB_CAPABILITIES == 0x7fu);
  assert(HID_RGB_BRIDGE_PROTOCOL_MAJOR == 1u);
  assert(sizeof(hid_packet_rgb_capabilities_t) == HID_PROTOCOL_PACKET_SIZE);
  assert(offsetof(hid_packet_rgb_capabilities_t, capabilities) == 8u);
  assert((HID_RGB_CAP_FRAME_CHUNKS & HID_RGB_CAP_PIXEL) == 0u);
  puts("rgb_bridge_protocol_test: ok");
  return 0;
}
