#ifndef KBHE_XINPUT_TEST_USBD_PVT_H_
#define KBHE_XINPUT_TEST_USBD_PVT_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  XFER_RESULT_SUCCESS = 0,
  XFER_RESULT_FAILED = 1,
} xfer_result_t;

typedef struct {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
} tusb_desc_interface_t;

typedef struct {
  uint8_t unused;
} tusb_control_request_t;

typedef struct {
  void (*init)(void);
  void (*reset)(uint8_t rhport);
  uint16_t (*open)(uint8_t rhport, const tusb_desc_interface_t *desc_intf,
                   uint16_t max_len);
  bool (*control_xfer_cb)(uint8_t rhport, uint8_t stage,
                          const tusb_control_request_t *request);
  bool (*xfer_cb)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result,
                  uint32_t xferred_bytes);
  void (*sof)(uint8_t rhport, uint32_t frame_count);
} usbd_class_driver_t;

#define TUSB_CLASS_VENDOR_SPECIFIC 0xFFu
#define TUSB_XFER_INTERRUPT 3u
#define TU_VERIFY(condition, value)                                            \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return (value);                                                          \
    }                                                                          \
  } while (0)
#define TU_ASSERT(condition, value) TU_VERIFY(condition, value)

bool usbd_edpt_busy(uint8_t rhport, uint8_t ep_addr);
bool usbd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer,
                    uint16_t total_bytes, bool in_isr);
bool usbd_open_edpt_pair(uint8_t rhport, const uint8_t *desc_ep,
                         uint8_t ep_count, uint8_t xfer_type,
                         uint8_t *ep_out, uint8_t *ep_in);
const uint8_t *tu_desc_next(const uint8_t *desc);

#endif
