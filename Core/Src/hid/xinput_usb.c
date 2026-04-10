#include "hid/xinput_usb.h"

#include "device/usbd_pvt.h"
#include "hid/gamepad_hid.h"
#include "settings.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include <string.h>

#define KBHE_GAMEPAD_BUTTON_BIT(button_enum)                                    \
  (1ul << ((uint32_t)(button_enum)-1u))

enum {
  KBHE_BUTTON_A = 1u,
  KBHE_BUTTON_B = 2u,
  KBHE_BUTTON_X = 3u,
  KBHE_BUTTON_Y = 4u,
  KBHE_BUTTON_L1 = 5u,
  KBHE_BUTTON_R1 = 6u,
  KBHE_BUTTON_L2 = 7u,
  KBHE_BUTTON_R2 = 8u,
  KBHE_BUTTON_SELECT = 9u,
  KBHE_BUTTON_START = 10u,
  KBHE_BUTTON_L3 = 11u,
  KBHE_BUTTON_R3 = 12u,
  KBHE_BUTTON_DPAD_UP = 13u,
  KBHE_BUTTON_DPAD_DOWN = 14u,
  KBHE_BUTTON_DPAD_LEFT = 15u,
  KBHE_BUTTON_DPAD_RIGHT = 16u,
  KBHE_BUTTON_HOME = 17u,
};

static uint8_t s_rhport = 0u;
static uint8_t s_ep_out = 0u;
static uint8_t s_ep_in = 0u;
static uint8_t s_out_buffer[XINPUT_EP_SIZE];

static xinput_report_t s_report = {
    .report_id = 0u,
    .report_size = sizeof(xinput_report_t),
};
static xinput_report_t s_previous_report = {
    .report_id = 0u,
    .report_size = sizeof(xinput_report_t),
};

static int16_t xinput_scale_axis(int8_t axis) {
  return (int16_t)((int16_t)axis * 258);
}

static uint16_t xinput_map_buttons(uint32_t buttons) {
  uint16_t mapped = 0u;

  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_A)) != 0u)
    mapped |= XINPUT_BUTTON_A;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_B)) != 0u)
    mapped |= XINPUT_BUTTON_B;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_X)) != 0u)
    mapped |= XINPUT_BUTTON_X;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_Y)) != 0u)
    mapped |= XINPUT_BUTTON_Y;

  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_DPAD_UP)) != 0u)
    mapped |= XINPUT_BUTTON_UP;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_DPAD_DOWN)) != 0u)
    mapped |= XINPUT_BUTTON_DOWN;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_DPAD_LEFT)) != 0u)
    mapped |= XINPUT_BUTTON_LEFT;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_DPAD_RIGHT)) != 0u)
    mapped |= XINPUT_BUTTON_RIGHT;

  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_START)) != 0u)
    mapped |= XINPUT_BUTTON_START;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_SELECT)) != 0u)
    mapped |= XINPUT_BUTTON_BACK;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_HOME)) != 0u)
    mapped |= XINPUT_BUTTON_HOME;

  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_L3)) != 0u)
    mapped |= XINPUT_BUTTON_LS;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_R3)) != 0u)
    mapped |= XINPUT_BUTTON_RS;

  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_L1)) != 0u)
    mapped |= XINPUT_BUTTON_LB;
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_R1)) != 0u)
    mapped |= XINPUT_BUTTON_RB;

  return mapped;
}

static void xinput_build_report(const gamepad_report_t *source,
                                xinput_report_t *target) {
  uint32_t buttons = 0u;
  uint8_t lt = 0u;
  uint8_t rt = 0u;

  if (source == NULL || target == NULL) {
    return;
  }

  buttons = source->buttons;
  lt = source->lt;
  rt = source->rt;

  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_L2)) != 0u) {
    lt = 255u;
  }
  if ((buttons & KBHE_GAMEPAD_BUTTON_BIT(KBHE_BUTTON_R2)) != 0u) {
    rt = 255u;
  }

  target->report_id = 0u;
  target->report_size = sizeof(xinput_report_t);
  target->buttons = xinput_map_buttons(buttons);
  target->lt = lt;
  target->rt = rt;
  target->lx = xinput_scale_axis(source->lx);
  target->ly = xinput_scale_axis(source->ly);
  target->rx = xinput_scale_axis(source->rx);
  target->ry = xinput_scale_axis(source->ry);
  memset(target->reserved, 0, sizeof(target->reserved));
}

void xinput_usb_init(void) {
  s_rhport = 0u;
  s_ep_out = 0u;
  s_ep_in = 0u;
  memset(s_out_buffer, 0, sizeof(s_out_buffer));
  memset(&s_report, 0, sizeof(s_report));
  memset(&s_previous_report, 0, sizeof(s_previous_report));
  s_report.report_size = sizeof(xinput_report_t);
  s_previous_report.report_size = sizeof(xinput_report_t);
}

void xinput_usb_task(void) {
  const gamepad_report_t *source = gamepad_hid_get_report();

  if (settings_get_gamepad_api_mode() != GAMEPAD_API_XINPUT) {
    return;
  }

  xinput_build_report(source, &s_report);

  if (s_ep_in == 0u || !tud_ready()) {
    return;
  }

  if (memcmp(&s_report, &s_previous_report, sizeof(s_report)) == 0) {
    return;
  }

  if (usbd_edpt_busy(s_rhport, s_ep_in)) {
    return;
  }

  if (usbd_edpt_xfer(s_rhport, s_ep_in, (uint8_t *)&s_report,
                     sizeof(s_report), false)) {
    memcpy(&s_previous_report, &s_report, sizeof(s_report));
  }
}

static void xinput_driver_init(void) {}

static void xinput_driver_reset(uint8_t rhport) {
  s_rhport = rhport;
  s_ep_out = 0u;
  s_ep_in = 0u;
}

static uint16_t xinput_driver_open(uint8_t rhport,
                                   tusb_desc_interface_t const *desc_intf,
                                   uint16_t max_len) {
  uint8_t const *desc_ep = NULL;

  (void)max_len;

  if (desc_intf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
      desc_intf->bInterfaceSubClass != XINPUT_SUBCLASS_DEFAULT ||
      desc_intf->bInterfaceProtocol != XINPUT_PROTOCOL_DEFAULT) {
    return 0u;
  }

  TU_VERIFY(desc_intf->bNumEndpoints == 2u, 0u);

  desc_ep = tu_desc_next((uint8_t const *)desc_intf);
  desc_ep = tu_desc_next(desc_ep);

  TU_ASSERT(usbd_open_edpt_pair(rhport, desc_ep, desc_intf->bNumEndpoints,
                                TUSB_XFER_INTERRUPT, &s_ep_out, &s_ep_in),
            0u);

  s_rhport = rhport;
  if (s_ep_out != 0u) {
    (void)usbd_edpt_xfer(rhport, s_ep_out, s_out_buffer,
                         sizeof(s_out_buffer), false);
  }

  return XINPUT_DESC_LEN;
}

static bool xinput_driver_control_xfer_cb(
    uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  (void)rhport;
  (void)stage;
  (void)request;
  return true;
}

static bool xinput_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr,
                                  xfer_result_t result,
                                  uint32_t xferred_bytes) {
  (void)result;
  (void)xferred_bytes;

  if (ep_addr == s_ep_out && s_ep_out != 0u) {
    (void)usbd_edpt_xfer(rhport, s_ep_out, s_out_buffer,
                         sizeof(s_out_buffer), true);
  }

  return true;
}

static usbd_class_driver_t const s_xinput_driver = {
    .init = xinput_driver_init,
    .reset = xinput_driver_reset,
    .open = xinput_driver_open,
    .control_xfer_cb = xinput_driver_control_xfer_cb,
    .xfer_cb = xinput_driver_xfer_cb,
    .sof = NULL,
};

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1u;
  return &s_xinput_driver;
}
