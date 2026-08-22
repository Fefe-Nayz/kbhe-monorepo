/*
 * flash_storage.c
 * Power-loss-safe, dual-bank object journal for STM32F723 persistence.
 */

#include "flash_storage.h"
#include "flash_storage_codec.h"
#include "stm32f7xx_hal.h"

#include <stddef.h>
#include <string.h>

/* Legacy snapshots/records in sector 7 remain readable during migration. */
#define FLASH_STORAGE_LEGACY_MAGIC 0x4B424653u /* "KBFS" */
#define FLASH_STORAGE_INDEX_CAPACITY 96u
#define FLASH_STORAGE_ASYNC_MAX_SEGMENTS 4u
#define FLASH_STORAGE_ASYNC_HASH_BYTES_PER_WORD 16u
#define FLASH_STORAGE_BANK_DATA_OFFSET                                      \
  ((uint32_t)sizeof(flash_storage_bank_header_t))

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t length;
  uint32_t sequence;
  uint32_t crc32;
} flash_storage_legacy_header_t;

typedef struct {
  bool used;
  uint16_t object_namespace;
  uint16_t object_id;
  uint16_t schema_version;
  uint32_t generation;
  uint32_t record_offset;
  uint32_t payload_offset;
  uint32_t length;
} flash_storage_index_entry_t;

typedef struct {
  uint8_t bank;
  bool bank_switch_pending;
  uint32_t bank_generation;
  uint32_t record_offset;
} flash_storage_write_target_t;

typedef enum {
  FLASH_STORAGE_ASYNC_PHASE_IDLE = 0,
  FLASH_STORAGE_ASYNC_PHASE_HASH_PAYLOAD,
  FLASH_STORAGE_ASYNC_PHASE_GC_BANK_HEADER,
  FLASH_STORAGE_ASYNC_PHASE_GC_COPY_OBJECT,
  FLASH_STORAGE_ASYNC_PHASE_WRITE_OBJECT,
  FLASH_STORAGE_ASYNC_PHASE_GC_COMMIT_BANK,
} flash_storage_async_phase_t;

typedef struct {
  bool busy;
  bool bank_switch_pending;
  uint8_t target_bank;
  uint8_t segment_count;
  flash_storage_async_phase_t phase;
  uint32_t target_bank_generation;
  uint32_t payload_size;
  uint32_t record_size;
  uint32_t record_offset;
  uint32_t absolute_addr;
  uint32_t program_offset;
  uint32_t hash_offset;
  uint32_t payload_crc_state;
  uint8_t gc_index;
  uint32_t gc_target_offset;
  uint32_t gc_source_offset;
  uint32_t gc_record_size;
  uint32_t gc_new_record_offsets[FLASH_STORAGE_INDEX_CAPACITY];
  flash_storage_segment_t segments[FLASH_STORAGE_ASYNC_MAX_SEGMENTS];
  flash_storage_bank_header_t bank_header;
  flash_storage_object_header_t header;
  flash_storage_commit_t commit;
} flash_storage_async_write_t;

_Static_assert(sizeof(flash_storage_legacy_header_t) == 16u,
               "legacy flash header layout changed");
_Static_assert((FLASH_STORAGE_BANK_DATA_OFFSET & 3u) == 0u,
               "bank data must be word aligned");

static flash_storage_index_entry_t object_index[FLASH_STORAGE_INDEX_CAPACITY];
static uint8_t active_bank = 0u;
static uint32_t active_bank_generation = 0u;
static bool active_bank_is_legacy = false;
static bool storage_ready = false;
static uint32_t next_write_offset = FLASH_STORAGE_BANK_DATA_OFFSET;
static bool object_index_overflow = false;
static bool spare_bank_ready = false;
static flash_storage_status_t last_status = FLASH_STORAGE_STATUS_OK;
static flash_storage_async_write_t async_write;
static flash_storage_metrics_t storage_metrics;

static uint32_t flash_storage_align4(uint32_t value) {
  return (value + 3u) & ~3u;
}

static bool flash_storage_range_fits(uint32_t offset, uint32_t len,
                                     uint32_t limit) {
  return offset <= limit && len <= (limit - offset);
}

static uint32_t flash_storage_bank_base(uint8_t bank) {
  return bank == 0u ? FLASH_STORAGE_BANK0_BASE_ADDR
                    : FLASH_STORAGE_BANK1_BASE_ADDR;
}

static uint32_t flash_storage_bank_sector(uint8_t bank) {
  return bank == 0u ? FLASH_STORAGE_BANK0_SECTOR
                    : FLASH_STORAGE_BANK1_SECTOR;
}

static const uint8_t *flash_storage_bank_ptr(uint8_t bank, uint32_t offset) {
  return (const uint8_t *)(uintptr_t)(flash_storage_bank_base(bank) + offset);
}

static const uint8_t *flash_storage_active_ptr(uint32_t offset) {
  return flash_storage_bank_ptr(active_bank, offset);
}

static void flash_storage_invalidate_cache_range(uint32_t address,
                                                 uint32_t length) {
  uint32_t aligned_address = address & ~31u;
  uint32_t end = address + length;
  uint32_t aligned_end = (end + 31u) & ~31u;

  if (length == 0u || end < address) {
    return;
  }
  /* Flash is memory mapped and D-cache is enabled by main(). These lines are
   * read-only to the CPU, so targeted invalidation avoids disturbing dirty RAM
   * or DMA-owned buffers while still making program/erase read-back coherent. */
  SCB_InvalidateDCache_by_Addr(
      (uint32_t *)(uintptr_t)aligned_address,
      (int32_t)(aligned_end - aligned_address));
  __DSB();
  __ISB();
}

static void flash_storage_refresh_data_cache(void) {
  flash_storage_invalidate_cache_range(
      FLASH_STORAGE_BANK0_BASE_ADDR,
      FLASH_STORAGE_BANK_COUNT * FLASH_STORAGE_BANK_SIZE);
}

static bool flash_storage_generation_is_newer(uint32_t candidate,
                                               uint32_t current) {
  return current == 0u || (int32_t)(candidate - current) > 0;
}

static flash_storage_index_entry_t *
flash_storage_find_index(uint16_t object_namespace, uint16_t object_id) {
  for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
    if (object_index[i].used &&
        object_index[i].object_namespace == object_namespace &&
        object_index[i].object_id == object_id) {
      return &object_index[i];
    }
  }
  return NULL;
}

static flash_storage_index_entry_t *flash_storage_allocate_index(void) {
  for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
    if (!object_index[i].used) {
      return &object_index[i];
    }
  }
  object_index_overflow = true;
  return NULL;
}

static void flash_storage_index_record(uint16_t object_namespace,
                                       uint16_t object_id,
                                       uint16_t schema_version,
                                       uint32_t generation,
                                       uint32_t record_offset,
                                       uint32_t payload_offset,
                                       uint32_t length) {
  flash_storage_index_entry_t *entry =
      flash_storage_find_index(object_namespace, object_id);

  if (entry != NULL &&
      !flash_storage_generation_is_newer(generation, entry->generation) &&
      generation != entry->generation) {
    return;
  }
  if (entry == NULL) {
    entry = flash_storage_allocate_index();
  }
  if (entry == NULL) {
    return;
  }

  entry->used = true;
  entry->object_namespace = object_namespace;
  entry->object_id = object_id;
  entry->schema_version = schema_version;
  entry->generation = generation == 0u ? 1u : generation;
  entry->record_offset = record_offset;
  entry->payload_offset = payload_offset;
  entry->length = length;
}

static uint32_t flash_storage_live_object_count(void) {
  uint32_t count = 0u;
  for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
    if (object_index[i].used) {
      count++;
    }
  }
  return count;
}

static uint32_t flash_storage_find_high_water(uint8_t bank) {
  uint32_t offset = FLASH_STORAGE_BANK_SIZE;

  while (offset >= 4u) {
    offset -= 4u;
    if (*(const uint32_t *)flash_storage_bank_ptr(bank, offset) !=
        FLASH_EMPTY_VALUE) {
      return offset + 4u;
    }
  }
  return 0u;
}

static void flash_storage_scan_records(void) {
  uint32_t high_water = flash_storage_find_high_water(active_bank);
  uint32_t offset = active_bank_is_legacy ? 0u
                                          : FLASH_STORAGE_BANK_DATA_OFFSET;

  memset(object_index, 0, sizeof(object_index));
  object_index_overflow = false;
  next_write_offset = high_water > offset ? high_water : offset;

  while (flash_storage_range_fits(offset, 4u, high_water)) {
    uint32_t magic = *(const uint32_t *)flash_storage_active_ptr(offset);

    if (magic == FLASH_STORAGE_LEGACY_MAGIC &&
        flash_storage_range_fits(offset,
                                 sizeof(flash_storage_legacy_header_t),
                                 high_water)) {
      flash_storage_legacy_header_t header;
      uint32_t payload_size = 0u;
      uint32_t record_size = 0u;

      memcpy(&header, flash_storage_active_ptr(offset), sizeof(header));
      if (header.length != 0u && header.length <= FLASH_STORAGE_BANK_SIZE) {
        payload_size = flash_storage_align4(header.length);
        record_size = sizeof(header) + payload_size;
        if (flash_storage_range_fits(offset, record_size,
                                     FLASH_STORAGE_BANK_SIZE) &&
            offset + record_size > high_water) {
          /* A legacy header has no independent header CRC, but reserving a
           * plausible torn tail is safer than reprogramming non-erased words. */
          next_write_offset = offset + record_size;
          break;
        }
        if (flash_storage_range_fits(offset, record_size, high_water) &&
            flash_storage_codec_crc32(
                flash_storage_active_ptr(offset + sizeof(header)),
                header.length) == header.crc32) {
          flash_storage_index_record(
              FLASH_STORAGE_NAMESPACE_SETTINGS,
              FLASH_STORAGE_SETTINGS_OBJECT_ID, 0u, header.sequence, offset,
              offset + sizeof(header), header.length);
          offset += record_size;
          continue;
        }
      }
    } else if (magic == FLASH_STORAGE_OBJECT_MAGIC &&
               flash_storage_range_fits(
                   offset, sizeof(flash_storage_object_header_t), high_water)) {
      flash_storage_object_header_t header;
      uint32_t payload_size = 0u;
      uint32_t record_size = 0u;
      uint32_t commit_offset = 0u;

      memcpy(&header, flash_storage_active_ptr(offset), sizeof(header));
      if (flash_storage_codec_header_is_valid(&header) &&
          flash_storage_codec_record_size(header.length, &payload_size,
                                           &record_size) &&
          flash_storage_range_fits(offset, record_size,
                                   FLASH_STORAGE_BANK_SIZE)) {
        flash_storage_commit_t commit;
        commit_offset = offset + sizeof(header) + payload_size;
        if (offset + record_size > next_write_offset) {
          /* Header CRC makes this boundary trustworthy even if payload/commit
           * programming was interrupted. */
          next_write_offset = offset + record_size;
        }
        if (flash_storage_range_fits(offset, record_size, high_water)) {
          memcpy(&commit, flash_storage_active_ptr(commit_offset),
                 sizeof(commit));
          if (flash_storage_codec_record_is_committed(
                  &header,
                  flash_storage_active_ptr(offset + sizeof(header)),
                  &commit)) {
            flash_storage_index_record(
                header.object_namespace, header.object_id,
                header.schema_version, header.generation, offset,
                offset + sizeof(header), header.length);
          }
        }
        offset += record_size;
        continue;
      }
    }

    /* Unknown or torn data: resynchronise at the next flash word. */
    offset += 4u;
  }
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

static void flash_storage_clear_status_flags(void) {
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR |
                         FLASH_FLAG_ERSERR);
}

static bool flash_storage_erase_bank_locked(uint8_t bank) {
  FLASH_EraseInitTypeDef erase_init = {0};
  uint32_t sector_error = 0u;

  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase_init.Sector = flash_storage_bank_sector(bank);
  erase_init.NbSectors = 1u;
  return HAL_FLASHEx_Erase(&erase_init, &sector_error) == HAL_OK;
}

static bool flash_storage_program_bank_header_locked(uint8_t bank,
                                                     uint32_t generation,
                                                     bool commit) {
  flash_storage_bank_header_t header;
  uint32_t base = flash_storage_bank_base(bank);

  flash_storage_codec_prepare_bank_header(&header, generation, false);
  if (!flash_storage_program_bytes_locked(
          base, &header,
          offsetof(flash_storage_bank_header_t, commit_magic))) {
    return false;
  }
  if (commit) {
    uint32_t commit_magic = FLASH_STORAGE_BANK_COMMIT_MAGIC;
    return flash_storage_program_bytes_locked(
        base + offsetof(flash_storage_bank_header_t, commit_magic),
        &commit_magic, sizeof(commit_magic));
  }
  return true;
}

static bool flash_storage_commit_bank_locked(uint8_t bank) {
  uint32_t commit_magic = FLASH_STORAGE_BANK_COMMIT_MAGIC;
  return flash_storage_program_bytes_locked(
      flash_storage_bank_base(bank) +
          offsetof(flash_storage_bank_header_t, commit_magic),
      &commit_magic, sizeof(commit_magic));
}

static bool flash_storage_bank_header_read(
    uint8_t bank, flash_storage_bank_header_t *header) {
  if (bank >= FLASH_STORAGE_BANK_COUNT || header == NULL) {
    return false;
  }
  memcpy(header, flash_storage_bank_ptr(bank, 0u), sizeof(*header));
  return flash_storage_codec_bank_header_is_valid(header);
}

static bool flash_storage_initialize_empty_bank(uint8_t bank,
                                                uint32_t generation) {
  bool success = false;

  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  success = flash_storage_erase_bank_locked(bank);
  if (success) {
    success =
        flash_storage_program_bank_header_locked(bank, generation, true);
  }
  HAL_FLASH_Lock();
  flash_storage_refresh_data_cache();

  if (success) {
    flash_storage_bank_header_t header;
    success = flash_storage_bank_header_read(bank, &header) &&
              header.generation == (generation == 0u ? 1u : generation);
  }
  return success;
}

static bool flash_storage_select_active_bank(void) {
  flash_storage_bank_header_t headers[FLASH_STORAGE_BANK_COUNT];
  uint8_t selected = 0u;

  flash_storage_refresh_data_cache();
  memcpy(&headers[0], flash_storage_bank_ptr(0u, 0u), sizeof(headers[0]));
  memcpy(&headers[1], flash_storage_bank_ptr(1u, 0u), sizeof(headers[1]));
  if (flash_storage_codec_select_bank(&headers[0], &headers[1], &selected)) {
    active_bank = selected;
    active_bank_generation = headers[selected].generation;
    active_bank_is_legacy = false;
    storage_ready = true;
    flash_storage_scan_records();
    return true;
  }

  /* Firmware <=2.0.5 stored an unbanked journal in sector 7. Keep it active
   * until the next write migrates all live objects into bank A. */
  active_bank = 1u;
  active_bank_generation = 0u;
  active_bank_is_legacy = true;
  storage_ready = true;
  flash_storage_scan_records();
  if (flash_storage_live_object_count() != 0u) {
    return true;
  }

  if (!flash_storage_initialize_empty_bank(0u, 1u)) {
    storage_ready = false;
    return false;
  }
  active_bank = 0u;
  active_bank_generation = 1u;
  active_bank_is_legacy = false;
  storage_ready = true;
  flash_storage_scan_records();
  return true;
}

/*
 * The STM32F723 has one Flash bank. A 128 KiB sector erase therefore stalls
 * instruction fetch for orders of magnitude longer than the 125 us scan
 * deadline. Keep the inactive journal sector erased before USB/scan startup so
 * a later compaction consists exclusively of one-word, budgeted programs.
 */
static bool flash_storage_prepare_spare_bank_at_boot(void) {
  uint8_t spare_bank = (uint8_t)(active_bank ^ 1u);
  bool success = true;

  flash_storage_invalidate_cache_range(flash_storage_bank_base(spare_bank),
                                       FLASH_STORAGE_BANK_SIZE);
  if (flash_storage_find_high_water(spare_bank) != 0u) {
    HAL_FLASH_Unlock();
    flash_storage_clear_status_flags();
    success = flash_storage_erase_bank_locked(spare_bank);
    HAL_FLASH_Lock();
    storage_metrics.boot_erase_count++;
    flash_storage_invalidate_cache_range(flash_storage_bank_base(spare_bank),
                                         FLASH_STORAGE_BANK_SIZE);
  }
  spare_bank_ready = success && flash_storage_find_high_water(spare_bank) == 0u;
  return spare_bank_ready;
}

static uint32_t flash_storage_next_generation(
    const flash_storage_index_entry_t *entry) {
  uint32_t generation = entry == NULL ? 1u : entry->generation + 1u;
  return generation == 0u ? 1u : generation;
}

static uint32_t flash_storage_next_bank_generation(void) {
  uint32_t generation = active_bank_generation + 1u;
  return generation == 0u ? 1u : generation;
}

static bool flash_storage_verify_written_object(
    uint8_t bank, uint32_t record_offset,
    const flash_storage_object_header_t *header, uint32_t payload_size) {
  flash_storage_commit_t commit;
  uint32_t commit_offset = record_offset + sizeof(*header) + payload_size;

  memcpy(&commit, flash_storage_bank_ptr(bank, commit_offset), sizeof(commit));
  return memcmp(flash_storage_bank_ptr(bank, record_offset), header,
                sizeof(*header)) == 0 &&
         flash_storage_codec_record_is_committed(
             header,
             flash_storage_bank_ptr(bank,
                                    record_offset + sizeof(*header)),
             &commit);
}

static bool flash_storage_compacted_size(
    uint16_t excluded_namespace, uint16_t excluded_id,
    uint32_t required_record_size, uint32_t *total_size) {
  uint32_t total = FLASH_STORAGE_BANK_DATA_OFFSET;

  if (total_size == NULL || required_record_size > FLASH_STORAGE_BANK_SIZE) {
    return false;
  }
  for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
    uint32_t payload_size = 0u;
    uint32_t record_size = 0u;
    const flash_storage_index_entry_t *entry = &object_index[i];
    if (!entry->used ||
        (entry->object_namespace == excluded_namespace &&
         entry->object_id == excluded_id)) {
      continue;
    }
    if (!flash_storage_codec_record_size(entry->length, &payload_size,
                                          &record_size) ||
        record_size > UINT32_MAX - total) {
      return false;
    }
    total += record_size;
  }
  if (required_record_size > UINT32_MAX - total) {
    return false;
  }
  total += required_record_size;
  if (total > FLASH_STORAGE_BANK_SIZE) {
    return false;
  }
  *total_size = total;
  return true;
}

static bool flash_storage_copy_live_objects_locked(
    uint8_t target_bank, uint16_t excluded_namespace, uint16_t excluded_id,
    uint32_t *target_offset) {
  uint32_t offset = FLASH_STORAGE_BANK_DATA_OFFSET;

  if (target_offset == NULL) {
    return false;
  }
  for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
    const flash_storage_index_entry_t *entry = &object_index[i];
    flash_storage_object_header_t header;
    flash_storage_commit_t commit;
    uint32_t payload_size = 0u;
    uint32_t record_size = 0u;
    uint32_t absolute_addr = 0u;
    bool success = false;

    if (!entry->used ||
        (entry->object_namespace == excluded_namespace &&
         entry->object_id == excluded_id)) {
      continue;
    }
    if (!flash_storage_codec_record_size(entry->length, &payload_size,
                                          &record_size) ||
        !flash_storage_range_fits(offset, record_size,
                                  FLASH_STORAGE_BANK_SIZE)) {
      return false;
    }
    flash_storage_codec_prepare_header(
        &header, entry->object_namespace, entry->object_id,
        entry->schema_version, flash_storage_active_ptr(entry->payload_offset),
        entry->length, entry->generation);
    commit.generation = entry->generation;
    commit.magic = FLASH_STORAGE_COMMIT_MAGIC;
    absolute_addr = flash_storage_bank_base(target_bank) + offset;

    success = flash_storage_program_bytes_locked(absolute_addr, &header,
                                                 sizeof(header));
    if (success) {
      success = flash_storage_program_bytes_locked(
          absolute_addr + sizeof(header),
          flash_storage_active_ptr(entry->payload_offset), entry->length);
    }
    if (success) {
      success = flash_storage_program_bytes_locked(
          absolute_addr + sizeof(header) + payload_size, &commit,
          sizeof(commit));
    }
    if (!success) {
      return false;
    }
    flash_storage_refresh_data_cache();
    if (!flash_storage_verify_written_object(target_bank, offset, &header,
                                              payload_size)) {
      return false;
    }
    offset += record_size;
  }

  *target_offset = offset;
  return true;
}

static flash_storage_status_t flash_storage_prepare_write_target(
    uint16_t object_namespace, uint16_t object_id, uint32_t record_size,
    flash_storage_write_target_t *target) {
  uint32_t compacted_size = 0u;
  bool success = false;

  if (!storage_ready || target == NULL) {
    return FLASH_STORAGE_STATUS_FLASH_ERROR;
  }
  memset(target, 0, sizeof(*target));
  if (!active_bank_is_legacy &&
      flash_storage_range_fits(next_write_offset, record_size,
                               FLASH_STORAGE_BANK_SIZE)) {
    target->bank = active_bank;
    target->record_offset = next_write_offset;
    return FLASH_STORAGE_STATUS_OK;
  }
  if (!flash_storage_compacted_size(object_namespace, object_id, record_size,
                                    &compacted_size)) {
    return FLASH_STORAGE_STATUS_NO_SPACE;
  }
  (void)compacted_size;

  target->bank = (uint8_t)(active_bank ^ 1u);
  target->bank_switch_pending = true;
  target->bank_generation = flash_storage_next_bank_generation();

  /* Sector erase is never allowed from an operational write path. The spare
   * sector is prepared once at boot; after one bank switch another reboot is
   * required before a second compaction can proceed without a multi-ms stall. */
  if (!spare_bank_ready) {
    storage_metrics.deferred_no_space_count++;
    return FLASH_STORAGE_STATUS_NO_SPACE;
  }
  spare_bank_ready = false;

  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  success = flash_storage_program_bank_header_locked(
      target->bank, target->bank_generation, false);
  if (success) {
    success = flash_storage_copy_live_objects_locked(
        target->bank, object_namespace, object_id, &target->record_offset);
  }
  HAL_FLASH_Lock();
  flash_storage_refresh_data_cache();
  return success ? FLASH_STORAGE_STATUS_OK : FLASH_STORAGE_STATUS_FLASH_ERROR;
}

static bool flash_storage_finish_bank_switch(
    const flash_storage_write_target_t *target) {
  flash_storage_bank_header_t header;

  if (target == NULL || !target->bank_switch_pending) {
    return true;
  }
  if (!flash_storage_bank_header_read(target->bank, &header) ||
      header.generation != target->bank_generation) {
    return false;
  }
  active_bank = target->bank;
  active_bank_generation = target->bank_generation;
  active_bank_is_legacy = false;
  storage_ready = true;
  flash_storage_scan_records();
  return true;
}

static bool flash_storage_segments_measure(
    const flash_storage_segment_t *segments, uint8_t segment_count,
    uint32_t *total_length, uint32_t *payload_crc32) {
  uint32_t total = 0u;
  uint32_t crc_state = 0xFFFFFFFFu;

  if (segments == NULL || segment_count == 0u || total_length == NULL ||
      payload_crc32 == NULL) {
    return false;
  }
  for (uint8_t i = 0u; i < segment_count; i++) {
    if ((segments[i].data == NULL && segments[i].length != 0u) ||
        segments[i].length > UINT32_MAX - total) {
      return false;
    }
    crc_state = flash_storage_codec_crc32_update(
        crc_state, segments[i].data, segments[i].length);
    total += segments[i].length;
  }
  if (total == 0u) {
    return false;
  }
  *total_length = total;
  *payload_crc32 = ~crc_state;
  return true;
}

static uint8_t flash_storage_segment_byte(
    const flash_storage_segment_t *segments, uint8_t segment_count,
    uint32_t logical_offset) {
  for (uint8_t i = 0u; i < segment_count; i++) {
    if (logical_offset < segments[i].length) {
      return ((const uint8_t *)segments[i].data)[logical_offset];
    }
    logical_offset -= segments[i].length;
  }
  return 0xFFu;
}

static bool flash_storage_program_segments_locked(
    uint32_t absolute_addr, const flash_storage_segment_t *segments,
    uint8_t segment_count, uint32_t payload_size) {
  for (uint32_t offset = 0u; offset < payload_size; offset += 4u) {
    uint8_t bytes[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint32_t word = FLASH_EMPTY_VALUE;
    for (uint32_t i = 0u; i < 4u; i++) {
      bytes[i] = flash_storage_segment_byte(segments, segment_count,
                                            offset + i);
    }
    memcpy(&word, bytes, sizeof(word));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, absolute_addr + offset,
                          word) != HAL_OK) {
      return false;
    }
  }
  return true;
}

static uint8_t flash_storage_async_payload_byte(uint32_t logical_offset) {
  return flash_storage_segment_byte(async_write.segments,
                                    async_write.segment_count,
                                    logical_offset);
}

static uint8_t flash_storage_async_record_byte(uint32_t absolute_offset) {
  uint32_t header_size = sizeof(async_write.header);
  uint32_t commit_offset = header_size + async_write.payload_size;

  if (absolute_offset < header_size) {
    return ((const uint8_t *)&async_write.header)[absolute_offset];
  }
  if (absolute_offset < header_size + async_write.header.length) {
    return flash_storage_async_payload_byte(absolute_offset - header_size);
  }
  if (absolute_offset < commit_offset) {
    return 0xFFu;
  }
  return ((const uint8_t *)&async_write.commit)[absolute_offset - commit_offset];
}

static bool flash_storage_program_word_verified(uint32_t address,
                                                uint32_t word) {
  uint32_t readback = 0u;
  HAL_StatusTypeDef program_status;

  /* Keep Flash locked between scheduler ticks.  The F7 program operation is
   * synchronous, so the only interval that needs write access is this single
   * bounded word operation. */
  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  program_status =
      HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word);
  HAL_FLASH_Lock();
  if (program_status != HAL_OK) {
    return false;
  }
  storage_metrics.programmed_words++;
  flash_storage_invalidate_cache_range(address, sizeof(word));
  memcpy(&readback, (const void *)(uintptr_t)address, sizeof(readback));
  return readback == word;
}

static bool flash_storage_async_prepare_next_gc_object(void) {
  while (async_write.gc_index < FLASH_STORAGE_INDEX_CAPACITY) {
    uint8_t index = async_write.gc_index++;
    const flash_storage_index_entry_t *entry = &object_index[index];
    uint32_t payload_size = 0u;
    uint32_t record_size = 0u;

    if (!entry->used ||
        (entry->object_namespace == async_write.header.object_namespace &&
         entry->object_id == async_write.header.object_id)) {
      continue;
    }
    if (!flash_storage_codec_record_size(entry->length, &payload_size,
                                          &record_size) ||
        !flash_storage_range_fits(async_write.gc_target_offset, record_size,
                                  FLASH_STORAGE_BANK_SIZE)) {
      return false;
    }
    async_write.gc_new_record_offsets[index] = async_write.gc_target_offset;
    async_write.gc_source_offset = entry->record_offset;
    async_write.gc_record_size = record_size;
    async_write.program_offset = 0u;
    async_write.phase = FLASH_STORAGE_ASYNC_PHASE_GC_COPY_OBJECT;
    return true;
  }

  async_write.record_offset = async_write.gc_target_offset;
  async_write.absolute_addr =
      flash_storage_bank_base(async_write.target_bank) +
      async_write.record_offset;
  async_write.program_offset = 0u;
  async_write.phase = FLASH_STORAGE_ASYNC_PHASE_WRITE_OBJECT;
  return true;
}

static void flash_storage_async_fail(void) {
  if (!async_write.bank_switch_pending) {
    /* Never re-use a partially programmed record tail. */
    uint32_t reserved_end = async_write.record_offset + async_write.record_size;
    if (reserved_end > next_write_offset &&
        reserved_end <= FLASH_STORAGE_BANK_SIZE) {
      next_write_offset = reserved_end;
    }
  } else {
    spare_bank_ready = false;
  }
  async_write.busy = false;
  async_write.phase = FLASH_STORAGE_ASYNC_PHASE_IDLE;
  last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
}

static bool flash_storage_async_finish(void) {
  if (async_write.bank_switch_pending) {
    for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
      if (!object_index[i].used ||
          (object_index[i].object_namespace ==
               async_write.header.object_namespace &&
           object_index[i].object_id == async_write.header.object_id)) {
        continue;
      }
      if (async_write.gc_new_record_offsets[i] == UINT32_MAX) {
        return false;
      }
      object_index[i].record_offset = async_write.gc_new_record_offsets[i];
      object_index[i].payload_offset =
          async_write.gc_new_record_offsets[i] +
          sizeof(flash_storage_object_header_t);
    }
    active_bank = async_write.target_bank;
    active_bank_generation = async_write.target_bank_generation;
    active_bank_is_legacy = false;
    spare_bank_ready = false;
  }

  flash_storage_index_record(
      async_write.header.object_namespace, async_write.header.object_id,
      async_write.header.schema_version, async_write.header.generation,
      async_write.record_offset,
      async_write.record_offset + sizeof(async_write.header),
      async_write.header.length);
  next_write_offset = async_write.record_offset + async_write.record_size;
  async_write.busy = false;
  async_write.phase = FLASH_STORAGE_ASYNC_PHASE_IDLE;
  last_status = FLASH_STORAGE_STATUS_OK;
  return true;
}

static bool flash_storage_migrate_legacy_at_boot(void) {
  flash_storage_write_target_t target = {0};
  bool success = true;

  if (!active_bank_is_legacy) {
    return true;
  }
  if (!spare_bank_ready) {
    return false;
  }

  target.bank = (uint8_t)(active_bank ^ 1u);
  target.bank_switch_pending = true;
  target.bank_generation = 1u;
  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  success = flash_storage_program_bank_header_locked(
      target.bank, target.bank_generation, false);
  if (success) {
    /* Namespace zero is invalid, so this excludes no live legacy object. */
    success = flash_storage_copy_live_objects_locked(
        target.bank, 0u, 0u, &target.record_offset);
  }
  if (success) {
    success = flash_storage_commit_bank_locked(target.bank);
  }
  HAL_FLASH_Lock();
  flash_storage_refresh_data_cache();
  if (!success || !flash_storage_finish_bank_switch(&target)) {
    return false;
  }
  spare_bank_ready = false;
  return true;
}

void flash_storage_init(void) {
  memset(&async_write, 0, sizeof(async_write));
  memset(&storage_metrics, 0, sizeof(storage_metrics));
  spare_bank_ready = false;
  last_status = FLASH_STORAGE_STATUS_OK;
  if (!flash_storage_select_active_bank()) {
    last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
    return;
  }
  if (!flash_storage_prepare_spare_bank_at_boot()) {
    last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
    return;
  }
  if (!flash_storage_migrate_legacy_at_boot() ||
      !flash_storage_prepare_spare_bank_at_boot()) {
    last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
  }
}

flash_storage_status_t flash_storage_object_read(
    uint16_t object_namespace, uint16_t object_id, void *buf,
    uint32_t capacity, uint32_t *actual_len, uint16_t *schema_version,
    uint32_t *generation) {
  flash_storage_index_entry_t *entry = NULL;

  if (!storage_ready || object_namespace == 0u) {
    return FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }
  entry = flash_storage_find_index(object_namespace, object_id);
  if (entry == NULL) {
    return FLASH_STORAGE_STATUS_NOT_FOUND;
  }
  if (actual_len != NULL) {
    *actual_len = entry->length;
  }
  if (schema_version != NULL) {
    *schema_version = entry->schema_version;
  }
  if (generation != NULL) {
    *generation = entry->generation;
  }
  if (buf == NULL || capacity < entry->length) {
    return FLASH_STORAGE_STATUS_BUFFER_TOO_SMALL;
  }
  memcpy(buf, flash_storage_active_ptr(entry->payload_offset), entry->length);
  return FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_read_range(
    uint16_t object_namespace, uint16_t object_id, uint32_t offset, void *buf,
    uint32_t len, uint32_t *actual_len, uint16_t *schema_version,
    uint32_t *generation) {
  flash_storage_index_entry_t *entry = NULL;

  if (!storage_ready || object_namespace == 0u || buf == NULL) {
    return FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }
  entry = flash_storage_find_index(object_namespace, object_id);
  if (entry == NULL) {
    return FLASH_STORAGE_STATUS_NOT_FOUND;
  }
  if (actual_len != NULL) {
    *actual_len = entry->length;
  }
  if (schema_version != NULL) {
    *schema_version = entry->schema_version;
  }
  if (generation != NULL) {
    *generation = entry->generation;
  }
  if (!flash_storage_range_fits(offset, len, entry->length)) {
    return FLASH_STORAGE_STATUS_BUFFER_TOO_SMALL;
  }
  memcpy(buf, flash_storage_active_ptr(entry->payload_offset + offset), len);
  return FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_write(
    uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
    const void *buf, uint32_t len, uint32_t expected_generation,
    uint32_t *new_generation) {
  flash_storage_index_entry_t *entry = NULL;
  flash_storage_object_header_t header;
  flash_storage_commit_t commit;
  flash_storage_write_target_t target;
  uint32_t payload_size = 0u;
  uint32_t record_size = 0u;
  uint32_t generation = 0u;
  uint32_t absolute_addr = 0u;
  bool success = false;

  if (async_write.busy) {
    return last_status = FLASH_STORAGE_STATUS_BUSY;
  }
  if (object_index_overflow || object_namespace == 0u || buf == NULL ||
      !flash_storage_codec_record_size(len, &payload_size, &record_size) ||
      record_size > FLASH_STORAGE_BANK_SIZE) {
    return last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }

  entry = flash_storage_find_index(object_namespace, object_id);
  if (entry == NULL &&
      flash_storage_live_object_count() >= FLASH_STORAGE_INDEX_CAPACITY) {
    return last_status = FLASH_STORAGE_STATUS_NO_SPACE;
  }
  if (expected_generation != FLASH_STORAGE_GENERATION_ANY &&
      expected_generation != (entry == NULL ? 0u : entry->generation)) {
    return last_status = FLASH_STORAGE_STATUS_GENERATION_MISMATCH;
  }
  generation = flash_storage_next_generation(entry);
  last_status = flash_storage_prepare_write_target(
      object_namespace, object_id, record_size, &target);
  if (last_status != FLASH_STORAGE_STATUS_OK) {
    return last_status;
  }

  flash_storage_codec_prepare_header(&header, object_namespace, object_id,
                                     schema_version, buf, len, generation);
  commit.generation = generation;
  commit.magic = FLASH_STORAGE_COMMIT_MAGIC;
  absolute_addr =
      flash_storage_bank_base(target.bank) + target.record_offset;

  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  success = flash_storage_program_bytes_locked(absolute_addr, &header,
                                               sizeof(header));
  if (success) {
    success = flash_storage_program_bytes_locked(
        absolute_addr + sizeof(header), buf, len);
  }
  if (success) {
    success = flash_storage_program_bytes_locked(
        absolute_addr + sizeof(header) + payload_size, &commit,
        sizeof(commit));
  }
  flash_storage_refresh_data_cache();
  if (success) {
    success = flash_storage_verify_written_object(
        target.bank, target.record_offset, &header, payload_size);
  }
  if (success && target.bank_switch_pending) {
    success = flash_storage_commit_bank_locked(target.bank);
  }
  HAL_FLASH_Lock();
  flash_storage_refresh_data_cache();

  if (!success) {
    if (!target.bank_switch_pending) {
      flash_storage_scan_records();
    }
    return last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
  }
  if (target.bank_switch_pending) {
    if (!flash_storage_finish_bank_switch(&target)) {
      return last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
    }
  } else {
    flash_storage_index_record(object_namespace, object_id, schema_version,
                               generation, target.record_offset,
                               target.record_offset + sizeof(header), len);
    next_write_offset = target.record_offset + record_size;
  }
  if (new_generation != NULL) {
    *new_generation = generation;
  }
  return last_status = FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_write_segments(
    uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
    const flash_storage_segment_t *segments, uint8_t segment_count,
    uint32_t expected_generation, uint32_t *new_generation) {
  flash_storage_index_entry_t *entry = NULL;
  flash_storage_object_header_t header;
  flash_storage_commit_t commit;
  flash_storage_write_target_t target;
  uint32_t payload_length = 0u;
  uint32_t payload_crc32 = 0u;
  uint32_t payload_size = 0u;
  uint32_t record_size = 0u;
  uint32_t generation = 0u;
  uint32_t absolute_addr = 0u;
  bool success = false;

  if (async_write.busy) {
    return last_status = FLASH_STORAGE_STATUS_BUSY;
  }
  if (object_index_overflow || object_namespace == 0u ||
      !flash_storage_segments_measure(segments, segment_count, &payload_length,
                                      &payload_crc32) ||
      !flash_storage_codec_record_size(payload_length, &payload_size,
                                       &record_size) ||
      record_size > FLASH_STORAGE_BANK_SIZE) {
    return last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }

  entry = flash_storage_find_index(object_namespace, object_id);
  if (entry == NULL &&
      flash_storage_live_object_count() >= FLASH_STORAGE_INDEX_CAPACITY) {
    return last_status = FLASH_STORAGE_STATUS_NO_SPACE;
  }
  if (expected_generation != FLASH_STORAGE_GENERATION_ANY &&
      expected_generation != (entry == NULL ? 0u : entry->generation)) {
    return last_status = FLASH_STORAGE_STATUS_GENERATION_MISMATCH;
  }
  generation = flash_storage_next_generation(entry);
  last_status = flash_storage_prepare_write_target(
      object_namespace, object_id, record_size, &target);
  if (last_status != FLASH_STORAGE_STATUS_OK) {
    return last_status;
  }

  flash_storage_codec_prepare_header_with_crc(
      &header, object_namespace, object_id, schema_version, payload_length,
      generation, payload_crc32);
  commit.generation = generation;
  commit.magic = FLASH_STORAGE_COMMIT_MAGIC;
  absolute_addr =
      flash_storage_bank_base(target.bank) + target.record_offset;

  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  success = flash_storage_program_bytes_locked(absolute_addr, &header,
                                               sizeof(header));
  if (success) {
    success = flash_storage_program_segments_locked(
        absolute_addr + sizeof(header), segments, segment_count, payload_size);
  }
  if (success) {
    success = flash_storage_program_bytes_locked(
        absolute_addr + sizeof(header) + payload_size, &commit,
        sizeof(commit));
  }
  flash_storage_refresh_data_cache();
  if (success) {
    success = flash_storage_verify_written_object(
        target.bank, target.record_offset, &header, payload_size);
  }
  if (success && target.bank_switch_pending) {
    success = flash_storage_commit_bank_locked(target.bank);
  }
  HAL_FLASH_Lock();
  flash_storage_refresh_data_cache();

  if (!success) {
    if (!target.bank_switch_pending) {
      flash_storage_scan_records();
    }
    return last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
  }
  if (target.bank_switch_pending) {
    if (!flash_storage_finish_bank_switch(&target)) {
      return last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
    }
  } else {
    flash_storage_index_record(object_namespace, object_id, schema_version,
                               generation, target.record_offset,
                               target.record_offset + sizeof(header),
                               payload_length);
    next_write_offset = target.record_offset + record_size;
  }
  if (new_generation != NULL) {
    *new_generation = generation;
  }
  return last_status = FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_write_segments_async_begin(
    uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
    const flash_storage_segment_t *segments, uint8_t segment_count,
    uint32_t expected_generation, uint32_t *new_generation) {
  flash_storage_index_entry_t *entry = NULL;
  flash_storage_write_target_t target;
  uint32_t generation = 0u;
  uint32_t len = 0u;
  uint32_t payload_size = 0u;
  uint32_t record_size = 0u;

  if (async_write.busy) {
    return last_status = FLASH_STORAGE_STATUS_BUSY;
  }
  if (object_index_overflow || object_namespace == 0u || segments == NULL ||
      segment_count == 0u ||
      segment_count > FLASH_STORAGE_ASYNC_MAX_SEGMENTS) {
    return last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }
  for (uint8_t i = 0u; i < segment_count; i++) {
    if ((segments[i].data == NULL && segments[i].length != 0u) ||
        segments[i].length > UINT32_MAX - len) {
      return last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    len += segments[i].length;
  }
  if (len == 0u ||
      !flash_storage_codec_record_size(len, &payload_size, &record_size) ||
      record_size > FLASH_STORAGE_BANK_SIZE) {
    return last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
  }

  entry = flash_storage_find_index(object_namespace, object_id);
  if (entry == NULL &&
      flash_storage_live_object_count() >= FLASH_STORAGE_INDEX_CAPACITY) {
    return last_status = FLASH_STORAGE_STATUS_NO_SPACE;
  }
  if (expected_generation != FLASH_STORAGE_GENERATION_ANY &&
      expected_generation != (entry == NULL ? 0u : entry->generation)) {
    return last_status = FLASH_STORAGE_STATUS_GENERATION_MISMATCH;
  }
  generation = flash_storage_next_generation(entry);
  memset(&target, 0, sizeof(target));
  if (!active_bank_is_legacy &&
      flash_storage_range_fits(next_write_offset, record_size,
                               FLASH_STORAGE_BANK_SIZE)) {
    target.bank = active_bank;
    target.record_offset = next_write_offset;
  } else {
    uint32_t compacted_size = 0u;
    if (!flash_storage_compacted_size(object_namespace, object_id, record_size,
                                      &compacted_size)) {
      return last_status = FLASH_STORAGE_STATUS_NO_SPACE;
    }
    (void)compacted_size;
    if (active_bank_is_legacy || !spare_bank_ready) {
      storage_metrics.deferred_no_space_count++;
      return last_status = FLASH_STORAGE_STATUS_NO_SPACE;
    }
    target.bank = (uint8_t)(active_bank ^ 1u);
    target.bank_switch_pending = true;
    target.bank_generation = flash_storage_next_bank_generation();
  }

  memset(&async_write, 0, sizeof(async_write));
  for (uint32_t i = 0u; i < FLASH_STORAGE_INDEX_CAPACITY; i++) {
    async_write.gc_new_record_offsets[i] = UINT32_MAX;
  }
  async_write.busy = true;
  async_write.bank_switch_pending = target.bank_switch_pending;
  async_write.target_bank = target.bank;
  async_write.segment_count = segment_count;
  async_write.target_bank_generation = target.bank_generation;
  async_write.payload_size = payload_size;
  async_write.record_size = record_size;
  async_write.record_offset = target.record_offset;
  async_write.absolute_addr =
      flash_storage_bank_base(target.bank) + target.record_offset;
  memcpy(async_write.segments, segments,
         (size_t)segment_count * sizeof(segments[0]));
  async_write.header.object_namespace = object_namespace;
  async_write.header.object_id = object_id;
  async_write.header.schema_version = schema_version;
  async_write.header.generation = generation;
  async_write.header.length = len;
  async_write.payload_crc_state = 0xFFFFFFFFu;
  async_write.phase = FLASH_STORAGE_ASYNC_PHASE_HASH_PAYLOAD;
  async_write.commit.generation = generation;
  async_write.commit.magic = FLASH_STORAGE_COMMIT_MAGIC;

  if (new_generation != NULL) {
    *new_generation = generation;
  }
  return last_status = FLASH_STORAGE_STATUS_OK;
}

flash_storage_status_t flash_storage_object_write_async_begin(
    uint16_t object_namespace, uint16_t object_id, uint16_t schema_version,
    const void *buf, uint32_t len, uint32_t expected_generation,
    uint32_t *new_generation) {
  const flash_storage_segment_t segment = {.data = buf, .length = len};
  return flash_storage_object_write_segments_async_begin(
      object_namespace, object_id, schema_version, &segment, 1u,
      expected_generation, new_generation);
}

uint32_t flash_storage_free_bytes(void) {
  return next_write_offset <= FLASH_STORAGE_BANK_SIZE
             ? FLASH_STORAGE_BANK_SIZE - next_write_offset
             : 0u;
}

flash_storage_status_t flash_storage_get_last_status(void) {
  return last_status;
}

bool flash_storage_erase(void) {
  uint8_t target_bank = 0u;
  uint32_t generation = 1u;
  bool success = false;

  if (async_write.busy) {
    last_status = FLASH_STORAGE_STATUS_BUSY;
    return false;
  }
  if (storage_ready) {
    target_bank = (uint8_t)(active_bank ^ 1u);
    generation = flash_storage_next_bank_generation();
  }

  /* Logical erase publishes the already-erased spare bank. Never start a
   * physical sector erase after scan startup. */
  if (!spare_bank_ready) {
    storage_metrics.deferred_no_space_count++;
    last_status = FLASH_STORAGE_STATUS_NO_SPACE;
    return false;
  }
  HAL_FLASH_Unlock();
  flash_storage_clear_status_flags();
  success = flash_storage_program_bank_header_locked(target_bank, generation,
                                                      true);
  HAL_FLASH_Lock();
  flash_storage_invalidate_cache_range(flash_storage_bank_base(target_bank),
                                       sizeof(flash_storage_bank_header_t));
  if (!success) {
    last_status = FLASH_STORAGE_STATUS_FLASH_ERROR;
    return false;
  }
  active_bank = target_bank;
  active_bank_generation = generation;
  active_bank_is_legacy = false;
  spare_bank_ready = false;
  storage_ready = true;
  flash_storage_scan_records();
  last_status = FLASH_STORAGE_STATUS_OK;
  return true;
}

bool flash_storage_read(uint32_t offset, void *buf, uint32_t len) {
  flash_storage_index_entry_t *entry = flash_storage_find_index(
      FLASH_STORAGE_NAMESPACE_SETTINGS, FLASH_STORAGE_SETTINGS_OBJECT_ID);

  if (entry == NULL || buf == NULL ||
      !flash_storage_range_fits(offset, len, entry->length)) {
    return false;
  }
  memcpy(buf, flash_storage_active_ptr(entry->payload_offset + offset), len);
  return true;
}

bool flash_storage_write(uint32_t offset, const void *buf, uint32_t len) {
  if (offset != 0u) {
    last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
    return false;
  }
  return flash_storage_object_write(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, 0u, buf, len,
             FLASH_STORAGE_GENERATION_ANY, NULL) == FLASH_STORAGE_STATUS_OK;
}

bool flash_storage_write_async_begin(uint32_t offset, const void *buf,
                                     uint32_t len) {
  if (offset != 0u) {
    last_status = FLASH_STORAGE_STATUS_INVALID_ARGUMENT;
    return false;
  }
  return flash_storage_object_write_async_begin(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, 0u, buf, len,
             FLASH_STORAGE_GENERATION_ANY, NULL) == FLASH_STORAGE_STATUS_OK;
}

flash_storage_async_result_t flash_storage_write_async_step(uint16_t max_words) {
  uint16_t words_used = 0u;

  if (!async_write.busy) {
    return last_status == FLASH_STORAGE_STATUS_FLASH_ERROR
               ? FLASH_STORAGE_ASYNC_ERROR
               : FLASH_STORAGE_ASYNC_DONE;
  }
  if (max_words == 0u) {
    return FLASH_STORAGE_ASYNC_IN_PROGRESS;
  }
  storage_metrics.async_steps++;

  while (max_words > 0u && async_write.busy) {
    uint8_t bytes[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint32_t word = FLASH_EMPTY_VALUE;
    uint32_t address = 0u;

    if (async_write.phase == FLASH_STORAGE_ASYNC_PHASE_HASH_PAYLOAD) {
      uint8_t hash_chunk[FLASH_STORAGE_ASYNC_HASH_BYTES_PER_WORD];
      uint32_t remaining =
          async_write.header.length - async_write.hash_offset;
      uint32_t chunk = remaining > sizeof(hash_chunk) ? sizeof(hash_chunk)
                                                       : remaining;

      for (uint32_t i = 0u; i < chunk; i++) {
        hash_chunk[i] = flash_storage_async_payload_byte(
            async_write.hash_offset + i);
      }
      async_write.payload_crc_state = flash_storage_codec_crc32_update(
          async_write.payload_crc_state, hash_chunk, chunk);
      async_write.hash_offset += chunk;
      max_words--;

      if (async_write.hash_offset < async_write.header.length) {
        continue;
      }

      flash_storage_codec_prepare_header_with_crc(
          &async_write.header, async_write.header.object_namespace,
          async_write.header.object_id, async_write.header.schema_version,
          async_write.header.length, async_write.header.generation,
          ~async_write.payload_crc_state);
      async_write.program_offset = 0u;
      if (async_write.bank_switch_pending) {
        flash_storage_codec_prepare_bank_header(
            &async_write.bank_header, async_write.target_bank_generation,
            false);
        async_write.absolute_addr =
            flash_storage_bank_base(async_write.target_bank);
        async_write.phase = FLASH_STORAGE_ASYNC_PHASE_GC_BANK_HEADER;
        spare_bank_ready = false;
        storage_metrics.async_gc_count++;
      } else {
        async_write.phase = FLASH_STORAGE_ASYNC_PHASE_WRITE_OBJECT;
      }
      continue;
    }

    if (async_write.phase == FLASH_STORAGE_ASYNC_PHASE_GC_BANK_HEADER) {
      uint32_t header_program_size =
          offsetof(flash_storage_bank_header_t, commit_magic);
      if (async_write.program_offset >= header_program_size) {
        async_write.gc_target_offset = FLASH_STORAGE_BANK_DATA_OFFSET;
        async_write.gc_index = 0u;
        if (!flash_storage_async_prepare_next_gc_object()) {
          flash_storage_async_fail();
          break;
        }
        continue;
      }
      memcpy(&word, (const uint8_t *)&async_write.bank_header +
                        async_write.program_offset,
             sizeof(word));
      address = async_write.absolute_addr + async_write.program_offset;
    } else if (async_write.phase ==
               FLASH_STORAGE_ASYNC_PHASE_GC_COPY_OBJECT) {
      if (async_write.program_offset >= async_write.gc_record_size) {
        async_write.gc_target_offset += async_write.gc_record_size;
        if (!flash_storage_async_prepare_next_gc_object()) {
          flash_storage_async_fail();
          break;
        }
        continue;
      }
      memcpy(&word,
             flash_storage_active_ptr(async_write.gc_source_offset +
                                      async_write.program_offset),
             sizeof(word));
      address = flash_storage_bank_base(async_write.target_bank) +
                async_write.gc_target_offset + async_write.program_offset;
    } else if (async_write.phase ==
               FLASH_STORAGE_ASYNC_PHASE_WRITE_OBJECT) {
      if (async_write.program_offset >= async_write.record_size) {
        if (async_write.bank_switch_pending) {
          async_write.program_offset = 0u;
          async_write.phase = FLASH_STORAGE_ASYNC_PHASE_GC_COMMIT_BANK;
          continue;
        }
        if (!flash_storage_async_finish()) {
          flash_storage_async_fail();
          break;
        }
        continue;
      }
      for (uint32_t i = 0u; i < sizeof(bytes); i++) {
        bytes[i] =
            flash_storage_async_record_byte(async_write.program_offset + i);
      }
      memcpy(&word, bytes, sizeof(word));
      address = async_write.absolute_addr + async_write.program_offset;
    } else if (async_write.phase ==
               FLASH_STORAGE_ASYNC_PHASE_GC_COMMIT_BANK) {
      word = FLASH_STORAGE_BANK_COMMIT_MAGIC;
      address = flash_storage_bank_base(async_write.target_bank) +
                offsetof(flash_storage_bank_header_t, commit_magic);
    } else {
      flash_storage_async_fail();
      break;
    }

    if (!flash_storage_program_word_verified(address, word)) {
      flash_storage_async_fail();
      break;
    }
    async_write.program_offset += 4u;
    max_words--;
    words_used++;

    if (async_write.phase == FLASH_STORAGE_ASYNC_PHASE_GC_COMMIT_BANK) {
      if (!flash_storage_async_finish()) {
        flash_storage_async_fail();
      }
      break;
    }
  }

  if (words_used > storage_metrics.max_words_in_one_step) {
    storage_metrics.max_words_in_one_step = words_used;
  }
  if (last_status == FLASH_STORAGE_STATUS_FLASH_ERROR) {
    return FLASH_STORAGE_ASYNC_ERROR;
  }
  return async_write.busy ? FLASH_STORAGE_ASYNC_IN_PROGRESS
                          : FLASH_STORAGE_ASYNC_DONE;
}

bool flash_storage_write_async_is_busy(void) { return async_write.busy; }

bool flash_storage_write_async_is_owner(uint16_t object_namespace,
                                        uint16_t object_id) {
  return async_write.busy &&
         async_write.header.object_namespace == object_namespace &&
         async_write.header.object_id == object_id;
}

void flash_storage_get_metrics(flash_storage_metrics_t *metrics_out) {
  if (metrics_out == NULL) {
    return;
  }
  *metrics_out = storage_metrics;
  metrics_out->async_busy = async_write.busy ? 1u : 0u;
  metrics_out->spare_bank_ready = spare_bank_ready ? 1u : 0u;
}

bool flash_storage_is_erased(uint32_t offset, uint32_t len) {
  if (!storage_ready ||
      !flash_storage_range_fits(offset, len, FLASH_STORAGE_BANK_SIZE)) {
    return false;
  }
  for (uint32_t i = 0u; i < len; i++) {
    if (*flash_storage_active_ptr(offset + i) != 0xFFu) {
      return false;
    }
  }
  return true;
}
