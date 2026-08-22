#include "updater_validation.h"

#include "updater_shared.h"

#include <stddef.h>

bool updater_version_is_strictly_newer(uint8_t candidate_major,
                                       uint8_t candidate_minor,
                                       uint8_t candidate_patch,
                                       uint8_t installed_major,
                                       uint8_t installed_minor,
                                       uint8_t installed_patch) {
  if (candidate_major != installed_major) {
    return candidate_major > installed_major;
  }
  if (candidate_minor != installed_minor) {
    return candidate_minor > installed_minor;
  }
  return candidate_patch > installed_patch;
}

bool updater_align_up_checked(uint32_t value, uint32_t align,
                              uint32_t *aligned_value) {
  uint32_t mask = 0u;

  if (aligned_value == NULL || align == 0u || (align & (align - 1u)) != 0u) {
    return false;
  }
  mask = align - 1u;
  if (value > UINT32_MAX - mask) {
    return false;
  }
  *aligned_value = (value + mask) & ~mask;
  return true;
}

uint32_t updater_align_up(uint32_t value, uint32_t align) {
  uint32_t aligned = UINT32_MAX;
  (void)updater_align_up_checked(value, align, &aligned);
  return aligned;
}

bool updater_vector_words_are_valid(uint32_t app_base, uint32_t image_size,
                                    uint32_t initial_sp,
                                    uint32_t reset_handler) {
  uint32_t image_end = 0u;
  uint32_t reset_handler_addr = reset_handler & ~1u;

  if (app_base != UPDATER_APP_BASE || image_size < 8u ||
      image_size > UPDATER_APP_MAX_IMAGE_SIZE ||
      app_base > UINT32_MAX - image_size) {
    return false;
  }
  image_end = app_base + image_size;
  if (image_end > UPDATER_TRAILER_ADDR || initial_sp <= UPDATER_RAM_BASE ||
      initial_sp > UPDATER_RAM_END || (initial_sp & 7u) != 0u ||
      (reset_handler & 1u) == 0u || reset_handler_addr < app_base ||
      reset_handler_addr >= image_end) {
    return false;
  }
  return true;
}

bool updater_data_padding_is_canonical(uint32_t image_size, uint32_t offset,
                                       const uint8_t *data,
                                       uint32_t data_length) {
  uint32_t end = 0u;
  uint32_t padding_start = 0u;

  if ((data == NULL && data_length != 0u) ||
      offset > UINT32_MAX - data_length) {
    return false;
  }
  end = offset + data_length;
  if (end <= image_size) {
    return true;
  }
  padding_start = offset < image_size ? image_size - offset : 0u;
  for (uint32_t i = padding_start; i < data_length; i++) {
    if (data[i] != UINT8_MAX) {
      return false;
    }
  }
  return true;
}
