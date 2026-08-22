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
//   Sector 3: 16KB  (0x0800C000 - 0x0800FFFF) updater version-floor journal
//   Sector 4: 64KB  (0x08010000 - 0x0801FFFF)
//   Sector 5: 128KB (0x08020000 - 0x0803FFFF)
//   Sector 6: 128KB (0x08040000 - 0x0805FFFF)
//   Sector 7: 128KB (0x08060000 - 0x0807FFFF)
//
// Sectors 6 and 7 form an A/B journal. A bank is committed only after all
// copied/live records are durable, allowing power-loss-safe garbage collection.
//--------------------------------------------------------------------+

#define FLASH_STORAGE_BANK_COUNT 2u
#define FLASH_STORAGE_WORD_PROGRAM_DATASHEET_MAX_US 100u
#define FLASH_STORAGE_BANK_SIZE (128u * 1024u)
#define FLASH_STORAGE_BANK0_SECTOR 6u
#define FLASH_STORAGE_BANK1_SECTOR 7u
#define FLASH_STORAGE_BANK0_BASE_ADDR 0x08040000u
#define FLASH_STORAGE_BANK1_BASE_ADDR 0x08060000u
/* Compatibility: maximum payload/bank size exposed to settings assertions. */
#define FLASH_STORAGE_BASE_ADDR FLASH_STORAGE_BANK1_BASE_ADDR
#define FLASH_STORAGE_SIZE FLASH_STORAGE_BANK_SIZE
#define FLASH_EMPTY_VALUE 0xFFFFFFFF

/*
 * Versioned object namespaces.  IDs are namespace-local, allowing settings,
 * action profiles and future macro/overlay stores to evolve independently.
 */
typedef enum {
	FLASH_STORAGE_NAMESPACE_SETTINGS = 1u,
	FLASH_STORAGE_NAMESPACE_ACTION_PROFILE = 2u,
	FLASH_STORAGE_NAMESPACE_MACRO = 3u,
	FLASH_STORAGE_NAMESPACE_OVERLAY = 4u,
	FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT = 5u,
} flash_storage_namespace_t;

#define FLASH_STORAGE_SETTINGS_OBJECT_ID 0u
#define FLASH_STORAGE_GENERATION_ANY UINT32_MAX

typedef enum {
	FLASH_STORAGE_STATUS_OK = 0,
	FLASH_STORAGE_STATUS_NOT_FOUND,
	FLASH_STORAGE_STATUS_INVALID_ARGUMENT,
	FLASH_STORAGE_STATUS_BUFFER_TOO_SMALL,
	FLASH_STORAGE_STATUS_GENERATION_MISMATCH,
	FLASH_STORAGE_STATUS_NO_SPACE,
	FLASH_STORAGE_STATUS_BUSY,
	FLASH_STORAGE_STATUS_FLASH_ERROR,
} flash_storage_status_t;

/**
 * Runtime persistence telemetry.  Physical sector erase is deliberately a
 * boot-only maintenance operation on STM32F723: its single Flash bank stalls
 * instruction fetch for far longer than one 125 us (8 kHz) scan period.
 */
typedef struct {
	uint32_t programmed_words;
	uint32_t async_steps;
	uint32_t async_gc_count;
	uint32_t boot_erase_count;
	uint32_t runtime_erase_count;
	uint32_t deferred_no_space_count;
	uint16_t max_words_in_one_step;
	uint8_t async_busy;
	uint8_t spare_bank_ready;
} flash_storage_metrics_t;

typedef struct {
	const void *data;
	uint32_t length;
} flash_storage_segment_t;

//--------------------------------------------------------------------+
// Flash API
//--------------------------------------------------------------------+

/**
 * @brief Initialize the flash storage driver
 */
void flash_storage_init(void);

/** Read the latest committed version of an object. */
flash_storage_status_t flash_storage_object_read(
		uint16_t object_namespace, uint16_t object_id, void *buf,
		uint32_t capacity, uint32_t *actual_len, uint16_t *schema_version,
		uint32_t *generation);

/** Read a bounded range without allocating storage for the whole object. */
flash_storage_status_t flash_storage_object_read_range(
		uint16_t object_namespace, uint16_t object_id, uint32_t offset,
		void *buf, uint32_t len, uint32_t *actual_len,
		uint16_t *schema_version, uint32_t *generation);

/**
 * Append and atomically commit a new object generation.
 *
 * expected_generation may be FLASH_STORAGE_GENERATION_ANY.  Otherwise it is
 * compared with the current generation (zero when the object does not exist),
 * providing a small compare-and-swap primitive for concurrent callers.
 */
flash_storage_status_t flash_storage_object_write(
		uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
		const void *buf, uint32_t len, uint32_t expected_generation,
		uint32_t *new_generation);

/** Atomically commit one logical object assembled from caller-owned segments. */
flash_storage_status_t flash_storage_object_write_segments(
		uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
		const flash_storage_segment_t *segments, uint8_t segment_count,
		uint32_t expected_generation, uint32_t *new_generation);

/** Begin an asynchronous versioned object write. */
flash_storage_status_t flash_storage_object_write_async_begin(
		uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
		const void *buf, uint32_t len, uint32_t expected_generation,
		uint32_t *new_generation);

/** Begin an asynchronous atomic object assembled from stable segments. */
flash_storage_status_t flash_storage_object_write_segments_async_begin(
		uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
		const flash_storage_segment_t *segments, uint8_t segment_count,
		uint32_t expected_generation, uint32_t *new_generation);

/** Number of erased bytes at the end of the active bank (GC may reclaim more). */
uint32_t flash_storage_free_bytes(void);

/** Last detailed status produced by a compatibility or async operation. */
flash_storage_status_t flash_storage_get_last_status(void);

/** Read persistence timing/maintenance counters without touching Flash. */
void flash_storage_get_metrics(flash_storage_metrics_t *metrics_out);

/**
 * @brief Atomically publish an empty newer bank (old bank remains recoverable)
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
 * @param max_words Maximum 32-bit words to program in this step. Passing zero
 *                  leaves the transaction untouched.
 * @return Async step status (IN_PROGRESS / DONE / ERROR)
 */
flash_storage_async_result_t flash_storage_write_async_step(uint16_t max_words);

/**
 * @brief Check whether an async flash write is currently active.
 */
bool flash_storage_write_async_is_busy(void);

/** True only when the singleton async writer belongs to this object. */
bool flash_storage_write_async_is_owner(uint16_t object_namespace,
                                        uint16_t object_id);

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
