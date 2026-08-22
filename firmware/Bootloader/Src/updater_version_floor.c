#include "updater_version_floor.h"

#include <stddef.h>
#include <string.h>

#if !defined(UPDATER_VERSION_FLOOR_HOST_TEST)
#include "stm32f7xx_hal.h"
#endif

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
  uint8_t reserved;
  uint32_t crc32;
  uint32_t commit_magic;
} updater_version_floor_entry_t;

#define UPDATER_VERSION_FLOOR_ENTRY_COUNT                                  \
  (UPDATER_VERSION_FLOOR_SIZE / sizeof(updater_version_floor_entry_t))

_Static_assert(sizeof(updater_version_floor_entry_t) ==
                   UPDATER_VERSION_FLOOR_ENTRY_SIZE,
               "version floor entry layout changed");
_Static_assert((UPDATER_VERSION_FLOOR_BASE & 3u) == 0u,
               "version floor must be word aligned");
_Static_assert((UPDATER_VERSION_FLOOR_SIZE %
                sizeof(updater_version_floor_entry_t)) == 0u,
               "version floor sector must contain complete entries");

#if defined(UPDATER_VERSION_FLOOR_HOST_TEST)
extern uint32_t updater_version_floor_test_read_word(uint32_t address);
extern bool updater_version_floor_test_program_word(uint32_t address,
                                                    uint32_t value);

static uint32_t floor_read_word(uint32_t address) {
  return updater_version_floor_test_read_word(address);
}

static bool floor_program_word(uint32_t address, uint32_t value) {
  return updater_version_floor_test_program_word(address, value);
}

static void floor_program_begin(void) {}
static void floor_program_end(void) {}
static void floor_refresh_cache(uint32_t address, uint32_t length) {
  (void)address;
  (void)length;
}
#else
static uint32_t floor_read_word(uint32_t address) {
  return *(const volatile uint32_t *)(uintptr_t)address;
}

static bool floor_program_word(uint32_t address, uint32_t value) {
  return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value) == HAL_OK;
}

static void floor_program_begin(void) {
  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                         FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                         FLASH_FLAG_PGPERR | FLASH_FLAG_ERSERR);
}

static void floor_program_end(void) { HAL_FLASH_Lock(); }

static void floor_refresh_cache(uint32_t address, uint32_t length) {
  uint32_t aligned_address = address & ~31u;
  uint32_t end = address + length;
  uint32_t aligned_end = (end + 31u) & ~31u;

  if (length == 0u || end < address) {
    return;
  }
  SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)aligned_address,
                              (int32_t)(aligned_end - aligned_address));
  __DSB();
  __ISB();
}
#endif

static uint32_t floor_crc32(const void *data, uint32_t length) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;

  while (length-- != 0u) {
    crc ^= *bytes++;
    for (uint32_t bit = 0u; bit < 8u; bit++) {
      crc = (crc >> 1) ^
            (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

static int floor_version_compare(updater_fw_version_t left,
                                 updater_fw_version_t right) {
  if (left.major != right.major) {
    return left.major < right.major ? -1 : 1;
  }
  if (left.minor != right.minor) {
    return left.minor < right.minor ? -1 : 1;
  }
  if (left.patch != right.patch) {
    return left.patch < right.patch ? -1 : 1;
  }
  return 0;
}

static uint32_t floor_entry_address(uint32_t index) {
  return UPDATER_VERSION_FLOOR_BASE +
         index * (uint32_t)sizeof(updater_version_floor_entry_t);
}

static void floor_entry_read(uint32_t index,
                             updater_version_floor_entry_t *entry) {
  uint32_t words[sizeof(*entry) / sizeof(uint32_t)];
  uint32_t address = floor_entry_address(index);

  for (uint32_t i = 0u; i < sizeof(words) / sizeof(words[0]); i++) {
    words[i] = floor_read_word(address + i * sizeof(uint32_t));
  }
  memcpy(entry, words, sizeof(*entry));
}

static bool floor_entry_is_erased(
    const updater_version_floor_entry_t *entry) {
  const uint8_t *bytes = (const uint8_t *)entry;
  for (uint32_t i = 0u; i < sizeof(*entry); i++) {
    if (bytes[i] != UINT8_MAX) {
      return false;
    }
  }
  return true;
}

static bool floor_entry_is_valid(
    const updater_version_floor_entry_t *entry) {
  return entry->magic == UPDATER_VERSION_FLOOR_MAGIC &&
         entry->reserved == 0u &&
         entry->commit_magic == UPDATER_VERSION_FLOOR_COMMIT_MAGIC &&
         entry->crc32 == floor_crc32(entry, offsetof(
                                                updater_version_floor_entry_t,
                                                crc32));
}

static bool floor_scan(updater_fw_version_t *version_out,
                       uint32_t *empty_index_out,
                       bool *programmed_out) {
  updater_fw_version_t greatest = {0};
  bool found = false;
  bool programmed = false;
  uint32_t empty_index = UINT32_MAX;

  for (uint32_t i = 0u; i < UPDATER_VERSION_FLOOR_ENTRY_COUNT; i++) {
    updater_version_floor_entry_t entry;
    floor_entry_read(i, &entry);
    if (floor_entry_is_erased(&entry)) {
      if (empty_index == UINT32_MAX) {
        empty_index = i;
      }
      continue;
    }
    programmed = true;
    if (floor_entry_is_valid(&entry)) {
      updater_fw_version_t version = {
          .major = entry.major,
          .minor = entry.minor,
          .patch = entry.patch,
      };
      if (!found || floor_version_compare(version, greatest) > 0) {
        greatest = version;
        found = true;
      }
    }
  }

  if (found && version_out != NULL) {
    *version_out = greatest;
  }
  if (empty_index_out != NULL) {
    *empty_index_out = empty_index;
  }
  if (programmed_out != NULL) {
    *programmed_out = programmed;
  }
  return found;
}

bool updater_version_floor_read(updater_fw_version_t *version_out) {
  if (version_out == NULL) {
    return false;
  }
  return floor_scan(version_out, NULL, NULL);
}

bool updater_version_floor_allows(updater_fw_version_t candidate) {
  updater_fw_version_t current = {0};
  bool programmed = false;
  if (!floor_scan(&current, NULL, &programmed)) {
    /* Only a wholly erased journal represents the intentional no-floor state.
     * Invalid programmed records with no surviving commit fail closed until
     * an explicit physical factory recovery restores a trusted baseline. */
    return !programmed;
  }
  return floor_version_compare(candidate, current) >= 0;
}

updater_version_floor_result_t updater_version_floor_prepare(
    updater_fw_version_t candidate) {
  updater_fw_version_t current = {0};
  updater_version_floor_entry_t entry;
  updater_version_floor_entry_t verify;
  uint32_t empty_index = UINT32_MAX;
  uint32_t address;
  uint32_t words[sizeof(entry) / sizeof(uint32_t)];
  bool programmed = false;
  bool has_floor = floor_scan(&current, &empty_index, &programmed);

  /* A non-erased journal with no surviving committed entry has no trustworthy
   * monotonic baseline. Do not let a later signed-but-older image redefine it;
   * this state requires an explicit physical factory recovery. */
  if (!has_floor && programmed) {
    return UPDATER_VERSION_FLOOR_STORAGE_ERROR;
  }

  if (has_floor) {
    int comparison = floor_version_compare(candidate, current);
    if (comparison < 0) {
      return UPDATER_VERSION_FLOOR_ROLLBACK;
    }
    if (comparison == 0) {
      return UPDATER_VERSION_FLOOR_OK;
    }
  }
  if (empty_index == UINT32_MAX) {
    return UPDATER_VERSION_FLOOR_STORAGE_ERROR;
  }

  memset(&entry, 0, sizeof(entry));
  entry.magic = UPDATER_VERSION_FLOOR_MAGIC;
  entry.major = candidate.major;
  entry.minor = candidate.minor;
  entry.patch = candidate.patch;
  entry.crc32 = floor_crc32(&entry, offsetof(updater_version_floor_entry_t,
                                             crc32));
  entry.commit_magic = UPDATER_VERSION_FLOOR_COMMIT_MAGIC;
  memcpy(words, &entry, sizeof(words));
  address = floor_entry_address(empty_index);

  floor_program_begin();
  for (uint32_t i = 0u; i < sizeof(words) / sizeof(words[0]); i++) {
    /* commit_magic is intentionally the final flash word. */
    if (!floor_program_word(address + i * sizeof(uint32_t), words[i])) {
      floor_program_end();
      floor_refresh_cache(address, sizeof(entry));
      return UPDATER_VERSION_FLOOR_STORAGE_ERROR;
    }
  }
  floor_program_end();
  floor_refresh_cache(address, sizeof(entry));

  floor_entry_read(empty_index, &verify);
  if (memcmp(&entry, &verify, sizeof(entry)) != 0 ||
      !floor_entry_is_valid(&verify)) {
    return UPDATER_VERSION_FLOOR_STORAGE_ERROR;
  }
  return UPDATER_VERSION_FLOOR_OK;
}
