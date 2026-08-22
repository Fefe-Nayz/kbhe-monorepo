#ifndef STM32F7XX_HAL_HOST_STUB_H_
#define STM32F7XX_HAL_HOST_STUB_H_

#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;

typedef struct {
  uint32_t TypeErase;
  uint32_t VoltageRange;
  uint32_t Sector;
  uint32_t NbSectors;
} FLASH_EraseInitTypeDef;

#define FLASH_TYPEPROGRAM_WORD 0u
#define FLASH_TYPEERASE_SECTORS 0u
#define FLASH_VOLTAGE_RANGE_3 0u
#define FLASH_SECTOR_4 4u
#define FLASH_FLAG_EOP 0u
#define FLASH_FLAG_OPERR 0u
#define FLASH_FLAG_WRPERR 0u
#define FLASH_FLAG_PGAERR 0u
#define FLASH_FLAG_PGPERR 0u
#define FLASH_FLAG_ERSERR 0u
#define __HAL_FLASH_CLEAR_FLAG(flags) ((void)(flags))

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address,
                                    uint64_t data);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,
                                    uint32_t *sector_error);
uint32_t HAL_GetTick(void);

static inline void SCB_InvalidateDCache_by_Addr(uint32_t *addr, int32_t size) {
  (void)addr;
  (void)size;
}

#define __DSB() ((void)0)
#define __ISB() ((void)0)

#endif
