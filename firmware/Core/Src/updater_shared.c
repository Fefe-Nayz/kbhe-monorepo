#include "updater_shared.h"
#include "stm32f7xx.h"

#include <string.h>

volatile boot_request_t g_boot_request
    __attribute__((section(".boot_shared"), aligned(4), used));

uint32_t updater_align_up(uint32_t value, uint32_t align) {
  if (align == 0u) {
    return value;
  }

  return (value + align - 1u) & ~(align - 1u);
}

void boot_request_clear(void) {
  g_boot_request.magic = 0u;
  g_boot_request.action = BOOT_REQUEST_ACTION_NONE;
  __DSB();
  __ISB();
}

void boot_request_set(boot_request_action_t action) {
  g_boot_request.magic = UPDATER_BOOT_REQUEST_MAGIC;
  g_boot_request.action = (uint32_t)action;
  __DSB();
  __ISB();
}

bool boot_request_take(boot_request_action_t action) {
  bool matched = (g_boot_request.magic == UPDATER_BOOT_REQUEST_MAGIC) &&
                 (g_boot_request.action == (uint32_t)action);
  if (matched) {
    boot_request_clear();
  }

  return matched;
}

uint32_t updater_crc32_compute(const void *data, uint32_t len) {
  const uint8_t *buf = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;

  while (len--) {
    crc ^= *buf++;
    for (uint32_t i = 0; i < 8u; i++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }

  return ~crc;
}

void updater_trailer_prepare(updater_trailer_t *trailer, uint32_t image_size,
                             uint32_t image_crc32, uint16_t fw_version) {
  memset(trailer, 0, sizeof(*trailer));
  trailer->magic = UPDATER_TRAILER_MAGIC;
  trailer->image_size = image_size;
  trailer->image_crc32 = image_crc32;
  trailer->fw_version = fw_version;
  trailer->trailer_crc32 =
      updater_crc32_compute(trailer, sizeof(*trailer) - sizeof(uint32_t));
}

static bool updater_trailer_is_sane(const updater_trailer_t *trailer) {
  if (trailer->magic != UPDATER_TRAILER_MAGIC) {
    return false;
  }

  if (trailer->image_size == 0u ||
      trailer->image_size > UPDATER_APP_MAX_IMAGE_SIZE) {
    return false;
  }

  return true;
}

bool updater_trailer_is_valid(const updater_trailer_t *trailer) {
  uint32_t computed_crc;

  if (!updater_trailer_is_sane(trailer)) {
    return false;
  }

  computed_crc =
      updater_crc32_compute(trailer, sizeof(*trailer) - sizeof(uint32_t));
  return computed_crc == trailer->trailer_crc32;
}

bool updater_read_trailer(updater_trailer_t *out_trailer) {
  const updater_trailer_t *flash_trailer =
      (const updater_trailer_t *)UPDATER_TRAILER_ADDR;

  memcpy(out_trailer, flash_trailer, sizeof(*out_trailer));
  return updater_trailer_is_valid(out_trailer);
}

bool updater_is_app_vector_valid(uint32_t app_base) {
  const uint32_t *vector = (const uint32_t *)app_base;
  uint32_t initial_sp = vector[0];
  uint32_t reset_handler = vector[1];
  uint32_t reset_handler_addr = reset_handler & ~1u;

  if (initial_sp < UPDATER_RAM_BASE || initial_sp > UPDATER_RAM_END) {
    return false;
  }

  if ((reset_handler & 1u) == 0u) {
    return false;
  }

  if (reset_handler_addr < UPDATER_APP_BASE ||
      reset_handler_addr >= UPDATER_TRAILER_ADDR) {
    return false;
  }

  return true;
}

bool updater_is_app_image_valid_with_trailer(
    const updater_trailer_t *trailer) {
  uint32_t computed_crc;

  if (!updater_trailer_is_valid(trailer)) {
    return false;
  }

  if (!updater_is_app_vector_valid(UPDATER_APP_BASE)) {
    return false;
  }

  computed_crc =
      updater_crc32_compute((const void *)UPDATER_APP_BASE, trailer->image_size);
  return computed_crc == trailer->image_crc32;
}

bool updater_is_app_image_valid(void) {
  updater_trailer_t trailer;

  if (!updater_read_trailer(&trailer)) {
    return false;
  }

  return updater_is_app_image_valid_with_trailer(&trailer);
}

uint16_t updater_get_app_version(void) {
  updater_trailer_t trailer;

  if (!updater_read_trailer(&trailer)) {
    return 0u;
  }

  if (!updater_is_app_image_valid_with_trailer(&trailer)) {
    return 0u;
  }

  return trailer.fw_version;
}
