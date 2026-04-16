/*
 * flash_storage.c
 * Append-only flash storage for STM32F723 settings persistence
 */

#include "flash_storage.h"
#include "stm32f7xx_hal.h"
#include <string.h>

//--------------------------------------------------------------------+
// Internal Types / Constants
//--------------------------------------------------------------------+

#define FLASH_STORAGE_RECORD_MAGIC 0x4B424653u /* "KBFS" */

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t length;
  uint32_t sequence;
  uint32_t crc32;
} flash_storage_record_header_t;

static uint32_t latest_record_offset = FLASH_STORAGE_SIZE;
static uint32_t latest_record_length = 0u;
static uint32_t next_write_offset = 0u;
static uint32_t next_sequence = 1u;

//--------------------------------------------------------------------+
// Internal Helpers
//--------------------------------------------------------------------+

static uint32_t flash_storage_align4(uint32_t value) {
  return (value + 3u) & ~3u;
}

static const uint8_t *flash_storage_ptr(uint32_t offset) {
  return (const uint8_t *)(FLASH_STORAGE_BASE_ADDR + offset);
}

static void flash_storage_reset_runtime_state(void) {
  latest_record_offset = FLASH_STORAGE_SIZE;
  latest_record_length = 0u;
  next_write_offset = 0u;
  next_sequence = 1u;
}

static uint32_t flash_storage_crc32_compute(const void *data, uint32_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;

  while (len-- != 0u) {
    crc ^= *bytes++;
    for (uint32_t bit = 0u; bit < 8u; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }

  return ~crc;
}

static bool flash_storage_program_bytes_locked(uint32_t absolute_addr,
                                               const void *buf,
                                               uint32_t len) {
  const uint8_t *bytes = (const uint8_t *)buf;
  uint32_t offset = 0u;

  while (offset < len) {
    uint32_t word = FLASH_EMPTY_VALUE;
    uint32_t chunk = len - offset;
    if (chunk > 4u) {
      chunk = 4u;
    }

    memcpy(&word, bytes + offset, chunk);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, absolute_addr + offset,
                          word) != HAL_OK) {
      return false;
    }

    offset += 4u;
  }

  return true;
}

static bool flash_storage_scan_records(void) {
  uint32_t offset = 0u;

  while (offset + sizeof(flash_storage_record_header_t) <= FLASH_STORAGE_SIZE) {
    uint32_t first_word = *(const uint32_t *)flash_storage_ptr(offset);
    flash_storage_record_header_t header = {0};
    uint32_t payload_size = 0u;
    uint32_t record_size = 0u;

    if (first_word == FLASH_EMPTY_VALUE) {
      break;
    }

    memcpy(&header, flash_storage_ptr(offset), sizeof(header));
    if (header.magic != FLASH_STORAGE_RECORD_MAGIC) {
      break;
    }

    if (header.length == 0u || header.length > FLASH_STORAGE_SIZE) {
      break;
    }

    payload_size = flash_storage_align4(header.length);
    record_size = sizeof(header) + payload_size;
    if (offset + record_size > FLASH_STORAGE_SIZE) {
      break;
    }

    if (flash_storage_crc32_compute(flash_storage_ptr(offset + sizeof(header)),
                                    header.length) != header.crc32) {
      break;
    }

    latest_record_offset = offset;
    latest_record_length = header.length;
    next_sequence = header.sequence + 1u;
    next_write_offset = offset + record_size;
    offset = next_write_offset;
  }

  return latest_record_offset < FLASH_STORAGE_SIZE;
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void flash_storage_init(void) {
  flash_storage_reset_runtime_state();
  (void)flash_storage_scan_records();
}

bool flash_storage_erase(void) {
  FLASH_EraseInitTypeDef erase_init = {0};
  uint32_t sector_error = 0u;
  bool success = true;

  HAL_FLASH_Unlock();

  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_ERSERR);

  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase_init.Sector = FLASH_STORAGE_SECTOR;
  erase_init.NbSectors = 1u;

  if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK) {
    success = false;
  }

  HAL_FLASH_Lock();

  if (success) {
    flash_storage_reset_runtime_state();
  }

  return success;
}

bool flash_storage_read(uint32_t offset, void *buf, uint32_t len) {
  uint32_t base_offset = 0u;

  if (buf == NULL) {
    return false;
  }

  if (latest_record_offset < FLASH_STORAGE_SIZE) {
    if (offset + len > latest_record_length) {
      return false;
    }
    base_offset = latest_record_offset + (uint32_t)sizeof(flash_storage_record_header_t);
  } else {
    return false;
  }

  memcpy(buf, flash_storage_ptr(base_offset + offset), len);
  return true;
}

bool flash_storage_write(uint32_t offset, const void *buf, uint32_t len) {
  flash_storage_record_header_t header = {0};
  uint32_t payload_size = 0u;
  uint32_t record_offset = 0u;
  uint32_t absolute_addr = 0u;
  bool success = true;

  if (buf == NULL || offset != 0u || len == 0u) {
    return false;
  }

  payload_size = flash_storage_align4(len);
  if ((uint32_t)sizeof(header) + payload_size > FLASH_STORAGE_SIZE) {
    return false;
  }

  if (next_write_offset + (uint32_t)sizeof(header) + payload_size >
          FLASH_STORAGE_SIZE) {
    if (!flash_storage_erase()) {
      return false;
    }
  }

  record_offset = next_write_offset;
  absolute_addr = FLASH_STORAGE_BASE_ADDR + record_offset;

  header.magic = FLASH_STORAGE_RECORD_MAGIC;
  header.length = len;
  header.sequence = next_sequence;
  header.crc32 = flash_storage_crc32_compute(buf, len);

  HAL_FLASH_Unlock();

  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_ERSERR);

  success =
      flash_storage_program_bytes_locked(absolute_addr, &header, sizeof(header));
  if (success) {
    success = flash_storage_program_bytes_locked(
        absolute_addr + (uint32_t)sizeof(header), buf, len);
  }

  HAL_FLASH_Lock();

  if (!success) {
    return false;
  }

  latest_record_offset = record_offset;
  latest_record_length = len;
  next_sequence++;
  next_write_offset =
      record_offset + (uint32_t)sizeof(header) + flash_storage_align4(len);

  return true;
}

bool flash_storage_is_erased(uint32_t offset, uint32_t len) {
  if (offset + len > FLASH_STORAGE_SIZE) {
    return false;
  }

  for (uint32_t i = 0u; i < len; i++) {
    if (*(const uint8_t *)flash_storage_ptr(offset + i) != 0xFFu) {
      return false;
    }
  }

  return true;
}
