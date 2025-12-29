/*
 * flash_storage.c
 * Flash storage driver for STM32F723 EEPROM emulation
 */

#include "flash_storage.h"
#include "stm32f7xx_hal.h"
#include <string.h>

//--------------------------------------------------------------------+
// Internal Functions
//--------------------------------------------------------------------+

void flash_storage_init(void) {
  // Nothing special to initialize for STM32F7 flash
}

bool flash_storage_erase(void) {
  FLASH_EraseInitTypeDef erase_init;
  uint32_t sector_error = 0;
  bool success = true;

  // Unlock flash
  HAL_FLASH_Unlock();

  // Clear any pending flags
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_ERSERR);

  // Configure erase
  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7V to 3.6V
  erase_init.Sector = FLASH_STORAGE_SECTOR;
  erase_init.NbSectors = 1;

  // Perform erase
  if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK) {
    success = false;
  }

  // Lock flash
  HAL_FLASH_Lock();

  return success;
}

bool flash_storage_read(uint32_t offset, void *buf, uint32_t len) {
  if (offset + len > FLASH_STORAGE_SIZE) {
    return false;
  }

  uint32_t addr = FLASH_STORAGE_BASE_ADDR + offset;
  memcpy(buf, (void *)addr, len);

  return true;
}

bool flash_storage_write(uint32_t offset, const void *buf, uint32_t len) {
  if (offset + len > FLASH_STORAGE_SIZE) {
    return false;
  }

  // Must be word-aligned
  if ((offset % 4 != 0) || (len % 4 != 0)) {
    return false;
  }

  const uint32_t *data = (const uint32_t *)buf;
  uint32_t addr = FLASH_STORAGE_BASE_ADDR + offset;
  uint32_t num_words = len / 4;
  bool success = true;

  // Unlock flash
  HAL_FLASH_Unlock();

  // Clear any pending flags
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_ERSERR);

  // Program word by word
  for (uint32_t i = 0; i < num_words && success; i++) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + (i * 4), data[i]) !=
        HAL_OK) {
      success = false;
    }
  }

  // Lock flash
  HAL_FLASH_Lock();

  return success;
}

bool flash_storage_is_erased(uint32_t offset, uint32_t len) {
  if (offset + len > FLASH_STORAGE_SIZE) {
    return false;
  }

  uint32_t addr = FLASH_STORAGE_BASE_ADDR + offset;

  // Check word by word
  for (uint32_t i = 0; i < len; i += 4) {
    if (*(uint32_t *)(addr + i) != FLASH_EMPTY_VALUE) {
      return false;
    }
  }

  return true;
}
