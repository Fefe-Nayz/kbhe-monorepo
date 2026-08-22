#include "updater_version_floor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error "Firmware host tests must not be built with NDEBUG"
#endif

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,    \
              #condition);                                                   \
      exit(EXIT_FAILURE);                                                    \
    }                                                                        \
  } while (0)

static uint8_t s_floor_flash[UPDATER_VERSION_FLOOR_SIZE];
static int32_t s_fail_before_call = -1;
static int32_t s_fail_after_program_call = -1;
static uint32_t s_program_calls;

uint32_t updater_version_floor_test_read_word(uint32_t address) {
  uint32_t value;
  uint32_t offset = address - UPDATER_VERSION_FLOOR_BASE;
  CHECK(address >= UPDATER_VERSION_FLOOR_BASE);
  CHECK(offset <= sizeof(s_floor_flash) - sizeof(value));
  CHECK((offset & 3u) == 0u);
  memcpy(&value, &s_floor_flash[offset], sizeof(value));
  return value;
}

bool updater_version_floor_test_program_word(uint32_t address,
                                             uint32_t value) {
  uint32_t current;
  uint32_t offset = address - UPDATER_VERSION_FLOOR_BASE;
  uint32_t call = s_program_calls++;

  CHECK(address >= UPDATER_VERSION_FLOOR_BASE);
  CHECK(offset <= sizeof(s_floor_flash) - sizeof(value));
  CHECK((offset & 3u) == 0u);
  if (s_fail_before_call >= 0 && call == (uint32_t)s_fail_before_call) {
    return false;
  }
  memcpy(&current, &s_floor_flash[offset], sizeof(current));
  CHECK((current & value) == value);
  current &= value;
  memcpy(&s_floor_flash[offset], &current, sizeof(current));
  return !(s_fail_after_program_call >= 0 &&
           call == (uint32_t)s_fail_after_program_call);
}

static updater_fw_version_t version(uint8_t major, uint8_t minor,
                                    uint8_t patch) {
  updater_fw_version_t value = {
      .major = major,
      .minor = minor,
      .patch = patch,
  };
  return value;
}

static void reset_failures(void) {
  s_fail_before_call = -1;
  s_fail_after_program_call = -1;
  s_program_calls = 0u;
}

static void check_floor(updater_fw_version_t expected) {
  updater_fw_version_t actual = {0};
  CHECK(updater_version_floor_read(&actual));
  CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
}

static void test_factory_seed_record_contract(void) {
  static const uint8_t factory_v1_2_3_entry[16] = {
      0x4c, 0x56, 0x46, 0x4b, 0x01, 0x02, 0x03, 0x00,
      0xa7, 0xb4, 0x22, 0x3d, 0x54, 0x4d, 0x4f, 0x43,
  };

  memset(s_floor_flash, 0xFF, sizeof(s_floor_flash));
  memcpy(s_floor_flash, factory_v1_2_3_entry,
         sizeof(factory_v1_2_3_entry));
  check_floor(version(1u, 2u, 3u));
}

static void test_monotonic_floor_and_power_cuts(void) {
  uint8_t baseline[sizeof(s_floor_flash)];
  updater_fw_version_t actual = {0};
  updater_fw_version_t v1 = version(1u, 0u, 0u);
  updater_fw_version_t v2 = version(1u, 1u, 0u);

  memset(s_floor_flash, 0xFF, sizeof(s_floor_flash));
  reset_failures();
  CHECK(!updater_version_floor_read(&actual));
  CHECK(updater_version_floor_allows(version(0u, 0u, 0u)));
  CHECK(updater_version_floor_prepare(v1) == UPDATER_VERSION_FLOOR_OK);
  check_floor(v1);
  CHECK(updater_version_floor_allows(v1));
  CHECK(updater_version_floor_allows(version(1u, 0u, 1u)));
  CHECK(!updater_version_floor_allows(version(0u, 255u, 255u)));
  CHECK(updater_version_floor_prepare(version(0u, 255u, 255u)) ==
        UPDATER_VERSION_FLOOR_ROLLBACK);

  reset_failures();
  CHECK(updater_version_floor_prepare(v1) == UPDATER_VERSION_FLOOR_OK);
  CHECK(s_program_calls == 0u);
  memcpy(baseline, s_floor_flash, sizeof(baseline));

  /* Failure before each of the four words, including the final commit, must
   * leave the previous floor authoritative. */
  for (int32_t cut = 0; cut < 4; cut++) {
    memcpy(s_floor_flash, baseline, sizeof(s_floor_flash));
    reset_failures();
    s_fail_before_call = cut;
    CHECK(updater_version_floor_prepare(v2) ==
          UPDATER_VERSION_FLOOR_STORAGE_ERROR);
    check_floor(v1);

    reset_failures();
    CHECK(updater_version_floor_prepare(v2) == UPDATER_VERSION_FLOOR_OK);
    check_floor(v2);
  }

  /* A controller error reported after the commit reached flash is recovered
   * as an equal-floor retry, never as a rollback. */
  memcpy(s_floor_flash, baseline, sizeof(s_floor_flash));
  reset_failures();
  s_fail_after_program_call = 3;
  CHECK(updater_version_floor_prepare(v2) ==
        UPDATER_VERSION_FLOOR_STORAGE_ERROR);
  check_floor(v2);
  reset_failures();
  CHECK(updater_version_floor_prepare(v2) == UPDATER_VERSION_FLOOR_OK);
  CHECK(s_program_calls == 0u);
}

static void test_full_or_corrupt_sector_fails_closed(void) {
  updater_fw_version_t actual = {0};

  memset(s_floor_flash, 0x00, sizeof(s_floor_flash));
  reset_failures();
  CHECK(!updater_version_floor_read(&actual));
  CHECK(!updater_version_floor_allows(version(0u, 0u, 0u)));
  CHECK(!updater_version_floor_allows(version(255u, 255u, 255u)));
  CHECK(updater_version_floor_prepare(version(9u, 9u, 9u)) ==
        UPDATER_VERSION_FLOOR_STORAGE_ERROR);
  CHECK(s_program_calls == 0u);
}

static void test_partial_first_record_requires_factory_recovery(void) {
  updater_fw_version_t v1 = version(1u, 0u, 0u);

  memset(s_floor_flash, 0xFF, sizeof(s_floor_flash));
  s_floor_flash[0] = 0u;
  reset_failures();
  CHECK(!updater_version_floor_allows(v1));
  CHECK(updater_version_floor_prepare(v1) ==
        UPDATER_VERSION_FLOOR_STORAGE_ERROR);
  CHECK(s_program_calls == 0u);
}

int main(void) {
  test_factory_seed_record_contract();
  test_monotonic_floor_and_power_cuts();
  test_full_or_corrupt_sector_fails_closed();
  test_partial_first_record_requires_factory_recovery();
  puts("updater_version_floor_test: ok");
  return EXIT_SUCCESS;
}
