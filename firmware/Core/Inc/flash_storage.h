/*
 * flash_storage.h
 * Flash storage driver for STM32F723 settings persistence
 */

#ifndef FLASH_STORAGE_H_
#define FLASH_STORAGE_H_

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Flash Configuration for STM32F723VET6
// Flash: 512KB total
// Sector layout:
//   Sector 0: 16KB  (0x08000000 - 0x08003FFF)
//   Sector 1: 16KB  (0x08004000 - 0x08007FFF)
//   Sector 2: 16KB  (0x08008000 - 0x0800BFFF)
//   Sector 3: 16KB  (0x0800C000 - 0x0800FFFF)
//   Sector 4: 64KB  (0x08010000 - 0x0801FFFF)
//   Sector 5: 128KB (0x08020000 - 0x0803FFFF)
//   Sector 6: 128KB (0x08040000 - 0x0805FFFF)
//   Sector 7: 128KB (0x08060000 - 0x0807FFFF)
//
// We'll use the last sector (7) for settings storage
//--------------------------------------------------------------------+

#define FLASH_STORAGE_SECTOR 7
#define FLASH_STORAGE_BASE_ADDR 0x08060000
#define FLASH_STORAGE_SIZE (128 * 1024) // 128KB
#define FLASH_EMPTY_VALUE 0xFFFFFFFF

//--------------------------------------------------------------------+
// Flash API
//--------------------------------------------------------------------+

/**
 * @brief Initialize the flash storage driver
 */
void flash_storage_init(void);

/**
 * @brief Erase the flash storage sector
 * @return true if successful, false otherwise
 */
bool flash_storage_erase(void);

/**
 * @brief Read data from flash storage
 * @param offset Offset from base address (in bytes)
 * @param buf Buffer to read into
 * @param len Length in bytes
 * @return true if successful, false otherwise
 */
bool flash_storage_read(uint32_t offset, void *buf, uint32_t len);

/**
 * @brief Append a new storage snapshot.
 *        Reads always target the latest valid snapshot.
 * @param offset Must be 0 for snapshot writes
 * @param buf Buffer to write from
 * @param len Length in bytes
 * @return true if successful, false otherwise
 */
bool flash_storage_write(uint32_t offset, const void *buf, uint32_t len);

typedef enum {
	FLASH_STORAGE_ASYNC_IN_PROGRESS = 0,
	FLASH_STORAGE_ASYNC_DONE = 1,
	FLASH_STORAGE_ASYNC_ERROR = 2,
} flash_storage_async_result_t;

/**
 * @brief Begin an asynchronous snapshot append.
 *
 * Call flash_storage_write_async_step() from the main loop until DONE or ERROR.
 *
 * @param offset Must be 0 for snapshot writes
 * @param buf Snapshot buffer to persist (must stay valid until completion)
 * @param len Snapshot length in bytes
 * @return true if the async write was started
 */
bool flash_storage_write_async_begin(uint32_t offset, const void *buf,
																		 uint32_t len);

/**
 * @brief Advance an in-progress asynchronous snapshot append.
 *
 * @param max_words Maximum 32-bit words to program in this step (0 -> 1)
 * @return Async step status (IN_PROGRESS / DONE / ERROR)
 */
flash_storage_async_result_t flash_storage_write_async_step(uint16_t max_words);

/**
 * @brief Check whether an async flash write is currently active.
 */
bool flash_storage_write_async_is_busy(void);

/**
 * @brief Check if a region is erased (all 0xFF)
 * @param offset Offset from base address
 * @param len Length in bytes
 * @return true if erased, false otherwise
 */
bool flash_storage_is_erased(uint32_t offset, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_STORAGE_H_ */
