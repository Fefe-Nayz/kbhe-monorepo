#include "hid_protocol.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
  assert(hid_ram_only_request_classify(0u, 1u, 0u) ==
         HID_RAM_ONLY_REQUEST_ENTER);
  assert(hid_ram_only_request_classify(0u, 0u, 0u) ==
         HID_RAM_ONLY_REQUEST_RELOAD_REBOOT);
  assert(hid_ram_only_request_classify(2u, 0u, 1u) ==
         HID_RAM_ONLY_REQUEST_LEAVE_V1);

  assert(hid_ram_only_request_classify(2u, 1u, 1u) ==
         HID_RAM_ONLY_REQUEST_INVALID);
  assert(hid_ram_only_request_classify(2u, 0u, 0u) ==
         HID_RAM_ONLY_REQUEST_INVALID);
  assert(hid_ram_only_request_classify(1u, 0u, 1u) ==
         HID_RAM_ONLY_REQUEST_INVALID);
  assert(hid_ram_only_request_classify(0u, 2u, 0u) ==
         HID_RAM_ONLY_REQUEST_INVALID);

  puts("ram_only_protocol_test: ok");
  return 0;
}
