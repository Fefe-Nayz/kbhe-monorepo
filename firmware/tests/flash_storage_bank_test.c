#include "flash_storage.h"
#include "flash_storage_codec.h"
#include "profile_document_store.h"
#include "stm32f7xx_hal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#define TEST_FLASH_BASE FLASH_STORAGE_BANK0_BASE_ADDR
#define TEST_FLASH_SIZE (FLASH_STORAGE_BANK_COUNT * FLASH_STORAGE_BANK_SIZE)

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t length;
  uint32_t sequence;
  uint32_t crc32;
} legacy_header_t;

static bool flash_unlocked;
static uint32_t erase_call_count;
static uint32_t program_call_count;
static uint32_t unlock_call_count;
static uint32_t lock_call_count;
static bool fail_next_program;

action_validation_result_t
action_engine_validate_program(const action_program_t *program) {
  if (program == NULL || program->version != ACTION_PROGRAM_VERSION) {
    return ACTION_VALIDATE_BAD_VERSION;
  }
  if (program->step_count == 0u ||
      program->step_count > ACTION_PROGRAM_MAX_STEPS) {
    return ACTION_VALIDATE_TOO_MANY_STEPS;
  }
  return ACTION_VALIDATE_OK;
}

uint16_t
action_engine_program_macro_dependencies(const action_program_t *program) {
  (void)program;
  return 0u;
}

action_validation_result_t
action_engine_validate_profile(const action_profile_t *profile) {
  if (profile == NULL) {
    return ACTION_VALIDATE_BAD_ARGUMENT;
  }
  for (uint8_t i = 0u; i < ACTION_PROGRAM_COUNT; i++) {
    action_validation_result_t result =
        action_engine_validate_program(&profile->programs[i]);
    if (result != ACTION_VALIDATE_OK) {
      return result;
    }
  }
  for (uint8_t i = 0u; i < LED_STATE_OVERLAY_COUNT; i++) {
    const action_overlay_binding_t *binding = &profile->overlays[i];
    if (binding->state_index >= ACTION_STATE_COUNT ||
        binding->active_value > 1u || binding->follows_state > 1u ||
        binding->config.blend_mode >= (uint8_t)LED_OVERLAY_BLEND_MAX) {
      return ACTION_VALIDATE_BAD_ARGUMENT;
    }
  }
  return ACTION_VALIDATE_OK;
}

static uint8_t *flash_ptr(uint32_t address) {
  assert(address >= TEST_FLASH_BASE);
  assert(address < TEST_FLASH_BASE + TEST_FLASH_SIZE);
  return (uint8_t *)(uintptr_t)address;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void) {
  flash_unlocked = true;
  unlock_call_count++;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void) {
  flash_unlocked = false;
  lock_call_count++;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address,
                                    uint64_t data) {
  uint32_t word = (uint32_t)data;
  uint32_t current = 0u;
  (void)type;
  if (!flash_unlocked || (address & 3u) != 0u ||
      address > TEST_FLASH_BASE + TEST_FLASH_SIZE - sizeof(word)) {
    return HAL_ERROR;
  }
  if (fail_next_program) {
    fail_next_program = false;
    return HAL_ERROR;
  }
  memcpy(&current, flash_ptr(address), sizeof(current));
  if ((current & word) != word) {
    return HAL_ERROR; /* STM32 flash cannot turn a programmed zero into one. */
  }
  current &= word;
  memcpy(flash_ptr(address), &current, sizeof(current));
  program_call_count++;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,
                                    uint32_t *sector_error) {
  uint32_t base = 0u;
  if (!flash_unlocked || erase == NULL || erase->NbSectors != 1u ||
      (erase->Sector != FLASH_STORAGE_BANK0_SECTOR &&
       erase->Sector != FLASH_STORAGE_BANK1_SECTOR)) {
    return HAL_ERROR;
  }
  base = erase->Sector == FLASH_STORAGE_BANK0_SECTOR
             ? FLASH_STORAGE_BANK0_BASE_ADDR
             : FLASH_STORAGE_BANK1_BASE_ADDR;
  memset(flash_ptr(base), 0xFF, FLASH_STORAGE_BANK_SIZE);
  erase_call_count++;
  if (sector_error != NULL) {
    *sector_error = UINT32_MAX;
  }
  return HAL_OK;
}

static void map_test_flash(void) {
#if defined(_WIN32)
  void *mapped = VirtualAlloc((void *)(uintptr_t)TEST_FLASH_BASE,
                              TEST_FLASH_SIZE,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  assert(mapped == (void *)(uintptr_t)TEST_FLASH_BASE);
#else
  void *mapped = mmap((void *)(uintptr_t)TEST_FLASH_BASE, TEST_FLASH_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  assert(mapped == (void *)(uintptr_t)TEST_FLASH_BASE);
#endif
  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
}

static void unmap_test_flash(void) {
#if defined(_WIN32)
  assert(VirtualFree((void *)(uintptr_t)TEST_FLASH_BASE, 0u, MEM_RELEASE));
#else
  assert(munmap((void *)(uintptr_t)TEST_FLASH_BASE, TEST_FLASH_SIZE) == 0);
#endif
}

static void test_gc_preserves_live_objects_and_replaces_target(void) {
  static uint8_t first[60000];
  static uint8_t second[60000];
  static uint8_t replacement[60000];
  static uint8_t output[60000];
  flash_storage_bank_header_t bank0;
  flash_storage_bank_header_t bank1;
  uint32_t generation = 0u;

  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
  memset(first, 0x11, sizeof(first));
  memset(second, 0x22, sizeof(second));
  memset(replacement, 0x33, sizeof(replacement));
  flash_storage_init();

  assert(flash_storage_object_write(FLASH_STORAGE_NAMESPACE_MACRO, 1u, 1u,
                                    first, sizeof(first), 0u,
                                    &generation) == FLASH_STORAGE_STATUS_OK);
  assert(generation == 1u);
  assert(flash_storage_object_write(FLASH_STORAGE_NAMESPACE_MACRO, 2u, 1u,
                                    second, sizeof(second), 0u,
                                    NULL) == FLASH_STORAGE_STATUS_OK);
  assert(flash_storage_object_write(FLASH_STORAGE_NAMESPACE_MACRO, 1u, 1u,
                                    replacement, sizeof(replacement), 1u,
                                    &generation) == FLASH_STORAGE_STATUS_OK);
  assert(generation == 2u); /* This update requires bank A -> B GC. */

  assert(flash_storage_object_read(FLASH_STORAGE_NAMESPACE_MACRO, 1u, output,
                                   sizeof(output), NULL, NULL, &generation) ==
         FLASH_STORAGE_STATUS_OK);
  assert(generation == 2u);
  assert(memcmp(output, replacement, sizeof(output)) == 0);
  assert(flash_storage_object_read(FLASH_STORAGE_NAMESPACE_MACRO, 2u, output,
                                   sizeof(output), NULL, NULL, NULL) ==
         FLASH_STORAGE_STATUS_OK);
  assert(memcmp(output, second, sizeof(output)) == 0);

  memcpy(&bank0, (const void *)(uintptr_t)FLASH_STORAGE_BANK0_BASE_ADDR,
         sizeof(bank0));
  memcpy(&bank1, (const void *)(uintptr_t)FLASH_STORAGE_BANK1_BASE_ADDR,
         sizeof(bank1));
  assert(flash_storage_codec_bank_header_is_valid(&bank0));
  assert(flash_storage_codec_bank_header_is_valid(&bank1));
  assert(bank0.generation == 1u);
  assert(bank1.generation == 2u);
}

static void test_power_cut_before_bank_commit_keeps_old_bank(void) {
  flash_storage_bank_header_t torn;
  uint8_t output[8] = {0};

  /* Previous test left committed generation 2 in bank B. Model a reboot after
   * bank A was erased/prepared but before its final commit word was written. */
  memset((void *)(uintptr_t)FLASH_STORAGE_BANK0_BASE_ADDR, 0xFF,
         FLASH_STORAGE_BANK_SIZE);
  flash_storage_codec_prepare_bank_header(&torn, 3u, false);
  memcpy((void *)(uintptr_t)FLASH_STORAGE_BANK0_BASE_ADDR, &torn,
         sizeof(torn));
  flash_storage_init();
  assert(flash_storage_object_read(FLASH_STORAGE_NAMESPACE_MACRO, 1u, output,
                                   sizeof(output), NULL, NULL, NULL) ==
         FLASH_STORAGE_STATUS_BUFFER_TOO_SMALL);
}

static void test_legacy_sector7_migrates_on_first_write(void) {
  const uint8_t legacy_payload[] = {9u, 8u, 7u, 6u};
  const uint8_t macro_payload[] = {1u, 3u, 5u, 7u};
  uint8_t output[sizeof(legacy_payload)] = {0};
  legacy_header_t header = {
      .magic = 0x4B424653u,
      .length = sizeof(legacy_payload),
      .sequence = 12u,
      .crc32 = flash_storage_codec_crc32(legacy_payload,
                                         sizeof(legacy_payload)),
  };
  flash_storage_bank_header_t migrated;

  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
  memcpy((void *)(uintptr_t)FLASH_STORAGE_BANK1_BASE_ADDR, &header,
         sizeof(header));
  memcpy((void *)(uintptr_t)(FLASH_STORAGE_BANK1_BASE_ADDR + sizeof(header)),
         legacy_payload, sizeof(legacy_payload));
  flash_storage_init();
  assert(flash_storage_object_read(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, output, sizeof(output), NULL,
             NULL, NULL) == FLASH_STORAGE_STATUS_OK);
  assert(memcmp(output, legacy_payload, sizeof(output)) == 0);

  assert(flash_storage_object_write(
             FLASH_STORAGE_NAMESPACE_MACRO, 4u, 1u, macro_payload,
             sizeof(macro_payload), 0u, NULL) == FLASH_STORAGE_STATUS_OK);
  memcpy(&migrated, (const void *)(uintptr_t)FLASH_STORAGE_BANK0_BASE_ADDR,
         sizeof(migrated));
  assert(flash_storage_codec_bank_header_is_valid(&migrated));
  assert(flash_storage_object_read(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, output, sizeof(output), NULL,
             NULL, NULL) == FLASH_STORAGE_STATUS_OK);
  assert(memcmp(output, legacy_payload, sizeof(output)) == 0);
}

static void test_async_gc_never_erases_and_respects_word_budget(void) {
  static uint8_t first[60000];
  static uint8_t second[60000];
  static uint8_t replacement[60000];
  static uint8_t output[60000];
  flash_storage_metrics_t metrics = {0};
  uint32_t erases_before = 0u;
  uint32_t generation = 0u;

  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
  memset(first, 0x41, sizeof(first));
  memset(second, 0x52, sizeof(second));
  memset(replacement, 0x63, sizeof(replacement));
  flash_storage_init();
  assert(flash_storage_object_write(FLASH_STORAGE_NAMESPACE_MACRO, 1u, 1u,
                                    first, sizeof(first), 0u, NULL) ==
         FLASH_STORAGE_STATUS_OK);
  assert(flash_storage_object_write(FLASH_STORAGE_NAMESPACE_MACRO, 2u, 1u,
                                    second, sizeof(second), 0u, NULL) ==
         FLASH_STORAGE_STATUS_OK);

  erases_before = erase_call_count;
  assert(flash_storage_object_write_async_begin(
             FLASH_STORAGE_NAMESPACE_MACRO, 1u, 1u, replacement,
             sizeof(replacement), 1u, &generation) == FLASH_STORAGE_STATUS_OK);
  assert(!flash_unlocked);
  assert(generation == 2u);
  for (;;) {
    uint32_t programs_before = program_call_count;
    flash_storage_async_result_t step = flash_storage_write_async_step(1u);
    assert(!flash_unlocked);
    assert(program_call_count - programs_before <= 1u);
    assert(erase_call_count == erases_before);
    if (step == FLASH_STORAGE_ASYNC_DONE) {
      break;
    }
    assert(step == FLASH_STORAGE_ASYNC_IN_PROGRESS);
  }

  flash_storage_get_metrics(&metrics);
  assert(metrics.async_gc_count == 1u);
  assert(metrics.max_words_in_one_step <= 1u);
  assert(metrics.runtime_erase_count == 0u);
  assert(metrics.async_busy == 0u);
  assert(metrics.spare_bank_ready == 0u);
  assert(flash_storage_object_read(FLASH_STORAGE_NAMESPACE_MACRO, 1u, output,
                                   sizeof(output), NULL, NULL, &generation) ==
         FLASH_STORAGE_STATUS_OK);
  assert(generation == 2u);
  assert(memcmp(output, replacement, sizeof(output)) == 0);
  assert(flash_storage_object_read(FLASH_STORAGE_NAMESPACE_MACRO, 2u, output,
                                   sizeof(output), NULL, NULL, NULL) ==
         FLASH_STORAGE_STATUS_OK);
  assert(memcmp(output, second, sizeof(output)) == 0);
}

static void test_async_writer_has_exclusive_owner_and_zero_budget_is_noop(void) {
  static const uint8_t macro_payload[] = {1u, 2u, 3u, 4u, 5u, 6u};
  static const uint8_t settings_payload[] = {9u, 8u, 7u, 6u};
  uint32_t programs_before = 0u;
  uint32_t unlocks_before = 0u;
  uint32_t locks_before = 0u;

  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
  flash_storage_init();
  assert(!flash_unlocked);
  assert(flash_storage_object_write_async_begin(
             FLASH_STORAGE_NAMESPACE_MACRO, 7u, 1u, macro_payload,
             sizeof(macro_payload), 0u, NULL) == FLASH_STORAGE_STATUS_OK);
  assert(!flash_unlocked);
  assert(flash_storage_write_async_is_owner(FLASH_STORAGE_NAMESPACE_MACRO,
                                            7u));
  assert(!flash_storage_write_async_is_owner(
      FLASH_STORAGE_NAMESPACE_SETTINGS, FLASH_STORAGE_SETTINGS_OBJECT_ID));

  /* A second client cannot replace the singleton owner. */
  assert(flash_storage_object_write_async_begin(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, 1u, settings_payload,
             sizeof(settings_payload), 0u, NULL) == FLASH_STORAGE_STATUS_BUSY);
  assert(flash_storage_write_async_is_owner(FLASH_STORAGE_NAMESPACE_MACRO,
                                            7u));

  programs_before = program_call_count;
  unlocks_before = unlock_call_count;
  locks_before = lock_call_count;
  assert(flash_storage_write_async_step(0u) ==
         FLASH_STORAGE_ASYNC_IN_PROGRESS);
  assert(program_call_count == programs_before);
  assert(unlock_call_count == unlocks_before);
  assert(lock_call_count == locks_before);
  assert(!flash_unlocked);

  while (flash_storage_write_async_step(1u) ==
         FLASH_STORAGE_ASYNC_IN_PROGRESS) {
    assert(!flash_unlocked);
  }
  assert(!flash_unlocked);
  assert(!flash_storage_write_async_is_busy());
  assert(!flash_storage_write_async_is_owner(FLASH_STORAGE_NAMESPACE_MACRO,
                                             7u));

  assert(flash_storage_object_write_async_begin(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, 1u, settings_payload,
             sizeof(settings_payload), 0u, NULL) == FLASH_STORAGE_STATUS_OK);
  assert(flash_storage_write_async_is_owner(
      FLASH_STORAGE_NAMESPACE_SETTINGS, FLASH_STORAGE_SETTINGS_OBJECT_ID));
  while (flash_storage_write_async_step(1u) ==
         FLASH_STORAGE_ASYNC_IN_PROGRESS) {
    assert(!flash_unlocked);
  }
  assert(!flash_unlocked);

  /* The error path must close the same narrow unlock window. */
  assert(flash_storage_object_write_async_begin(
             FLASH_STORAGE_NAMESPACE_MACRO, 8u, 1u, macro_payload,
             sizeof(macro_payload), 0u, NULL) == FLASH_STORAGE_STATUS_OK);
  fail_next_program = true;
  while (flash_storage_write_async_step(1u) ==
         FLASH_STORAGE_ASYNC_IN_PROGRESS) {
    assert(!flash_unlocked);
  }
  assert(!fail_next_program);
  assert(!flash_unlocked);
  assert(!flash_storage_write_async_is_busy());
}

static void test_raw_worst_case_capacity_is_explicit(void) {
  static uint8_t compact_settings[476];
  static uint8_t raw_document[16u + sizeof(settings_profile_t) +
                              sizeof(action_profile_t)];
  uint32_t erases_after_boot = 0u;
  uint32_t successful_updates = 0u;
  flash_storage_metrics_t metrics = {0};

  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
  memset(compact_settings, 0x6Bu, sizeof(compact_settings));
  memset(raw_document, 0xA7u, sizeof(raw_document));
  flash_storage_init();
  erases_after_boot = erase_call_count;

  assert(flash_storage_object_write(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, 1u, compact_settings,
             sizeof(compact_settings), 0u, NULL) == FLASH_STORAGE_STATUS_OK);
  for (uint8_t profile = 0u; profile < SETTINGS_PROFILE_COUNT; profile++) {
    assert(flash_storage_object_write(
               FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, profile, 2u,
               raw_document, sizeof(raw_document), 0u, NULL) ==
           FLASH_STORAGE_STATUS_OK);
  }

  for (;;) {
    flash_storage_status_t begin = flash_storage_object_write_async_begin(
        FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, 0u, 2u, raw_document,
        sizeof(raw_document), FLASH_STORAGE_GENERATION_ANY, NULL);
    if (begin == FLASH_STORAGE_STATUS_NO_SPACE) {
      break;
    }
    assert(begin == FLASH_STORAGE_STATUS_OK);
    for (;;) {
      uint32_t programs_before = program_call_count;
      flash_storage_async_result_t step = flash_storage_write_async_step(1u);
      assert(program_call_count - programs_before <= 1u);
      assert(!flash_unlocked);
      if (step == FLASH_STORAGE_ASYNC_DONE) {
        break;
      }
      assert(step == FLASH_STORAGE_ASYNC_IN_PROGRESS);
    }
    successful_updates++;
    assert(successful_updates < 20u);
  }

  /* With four incompressible 13,279-byte documents plus a 476-byte global
   * record, dual-bank no-erase operation has exactly eleven replacement
   * commits before a reboot is needed to erase the old bank. */
  assert(successful_updates == 11u);
  assert(erase_call_count == erases_after_boot);
  flash_storage_get_metrics(&metrics);
  assert(metrics.async_gc_count == 1u);
  assert(metrics.runtime_erase_count == 0u);
  assert(metrics.deferred_no_space_count == 1u);
}

static void valid_action_profile(action_profile_t *actions) {
  memset(actions, 0, sizeof(*actions));
  for (uint8_t program = 0u; program < ACTION_PROGRAM_COUNT; program++) {
    actions->programs[program].version = ACTION_PROGRAM_VERSION;
    actions->programs[program].step_count = 1u;
    actions->programs[program].steps[0].opcode = (uint8_t)ACTION_OP_END;
  }
}

static void test_compressed_profile_stress_100_saves_without_runtime_erase(void) {
  static settings_profile_t settings;
  static settings_profile_t loaded_settings;
  static action_profile_t actions;
  static action_profile_t loaded_actions;
  static uint8_t compact_settings[476];
  uint32_t pseudo_random = 0x8D31A5C7u;
  uint32_t erases_after_boot = 0u;
  uint32_t payload_length = 0u;
  uint8_t marker = 0u;
  flash_storage_metrics_t metrics = {0};

  memset((void *)(uintptr_t)TEST_FLASH_BASE, 0xFF, TEST_FLASH_SIZE);
  memset(&settings, 0, sizeof(settings));
  for (uint32_t i = 0u; i < 1900u; i++) {
    pseudo_random ^= pseudo_random << 13;
    pseudo_random ^= pseudo_random >> 17;
    pseudo_random ^= pseudo_random << 5;
    ((uint8_t *)&settings)[i] = (uint8_t)pseudo_random;
  }
  valid_action_profile(&actions);
  memset(compact_settings, 0x39, sizeof(compact_settings));
  flash_storage_init();
  erases_after_boot = erase_call_count;

  assert(flash_storage_object_write(
             FLASH_STORAGE_NAMESPACE_SETTINGS,
             FLASH_STORAGE_SETTINGS_OBJECT_ID, 1u, compact_settings,
             sizeof(compact_settings), 0u, NULL) == FLASH_STORAGE_STATUS_OK);
  for (uint8_t profile = 0u; profile < SETTINGS_PROFILE_COUNT; profile++) {
    assert(profile_document_store_save(profile, &settings, &actions, 0u,
                                       NULL));
  }
  assert(flash_storage_object_read_range(
             FLASH_STORAGE_NAMESPACE_PROFILE_DOCUMENT, 0u, 0u, &marker,
             sizeof(marker), &payload_length, NULL, NULL) ==
         FLASH_STORAGE_STATUS_OK);
  assert(payload_length >= 1800u && payload_length <= 2600u);

  for (uint32_t save = 0u; save < 100u; save++) {
    profile_document_async_result_t result =
        PROFILE_DOCUMENT_ASYNC_IN_PROGRESS;
    ((uint8_t *)&settings)[save % 128u] ^= (uint8_t)(save + 1u);
    assert(profile_document_store_save_async_begin(
        0u, &settings, &actions, FLASH_STORAGE_GENERATION_ANY));
    while (result == PROFILE_DOCUMENT_ASYNC_IN_PROGRESS) {
      uint32_t programs_before = program_call_count;
      profile_document_store_async_task(32u, 1u);
      assert(program_call_count - programs_before <= 1u);
      assert(erase_call_count == erases_after_boot);
      assert(!flash_unlocked);
      result = profile_document_store_async_result(NULL);
    }
    assert(result == PROFILE_DOCUMENT_ASYNC_DONE);
    profile_document_store_async_consume();
  }

  flash_storage_get_metrics(&metrics);
  assert(metrics.async_gc_count == 1u);
  assert(metrics.runtime_erase_count == 0u);
  assert(metrics.deferred_no_space_count == 0u);
  assert(metrics.max_words_in_one_step <= 1u);
  assert(erase_call_count == erases_after_boot);

  /* A boot may erase only the old inactive bank; the latest compressed
   * settings/actions pair must remain coherent. */
  flash_storage_init();
  assert(profile_document_store_load(0u, &loaded_settings, &loaded_actions,
                                     NULL));
  assert(memcmp(&loaded_settings, &settings, sizeof(settings)) == 0);
  assert(memcmp(&loaded_actions, &actions, sizeof(actions)) == 0);
}

int main(void) {
  map_test_flash();
  test_gc_preserves_live_objects_and_replaces_target();
  test_power_cut_before_bank_commit_keeps_old_bank();
  test_legacy_sector7_migrates_on_first_write();
  test_async_writer_has_exclusive_owner_and_zero_budget_is_noop();
  test_async_gc_never_erases_and_respects_word_budget();
  test_raw_worst_case_capacity_is_explicit();
  test_compressed_profile_stress_100_saves_without_runtime_erase();
  unmap_test_flash();
  puts("flash_storage_bank_test: ok");
  return 0;
}
