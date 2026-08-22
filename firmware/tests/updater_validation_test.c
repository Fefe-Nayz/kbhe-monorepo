#include "updater_shared.h"
#include "updater_validation.h"

#include <assert.h>
#include <stdio.h>

static void test_checked_alignment(void) {
  uint32_t aligned = 0u;
  assert(updater_align_up_checked(1u, 4u, &aligned) && aligned == 4u);
  assert(updater_align_up_checked(4u, 4u, &aligned) && aligned == 4u);
  assert(!updater_align_up_checked(UINT32_MAX, 4u, &aligned));
  assert(!updater_align_up_checked(8u, 3u, &aligned));
  assert(!updater_align_up_checked(8u, 0u, &aligned));
}

static void test_vector_must_be_inside_declared_image(void) {
  const uint32_t sp = UPDATER_RAM_END;
  assert(updater_vector_words_are_valid(UPDATER_APP_BASE, 0x100u, sp,
                                        UPDATER_APP_BASE + 0x81u));
  assert(!updater_vector_words_are_valid(UPDATER_APP_BASE, 0x80u, sp,
                                         UPDATER_APP_BASE + 0x81u));
  assert(!updater_vector_words_are_valid(UPDATER_APP_BASE, 0x100u, sp,
                                         UPDATER_APP_BASE + 0x80u));
  assert(!updater_vector_words_are_valid(UPDATER_APP_BASE, 7u, sp,
                                         UPDATER_APP_BASE + 1u));
}

static void test_vector_ram_and_slot_bounds(void) {
  assert(!updater_vector_words_are_valid(
      UPDATER_APP_BASE, 0x100u, UPDATER_RAM_BASE,
      UPDATER_APP_BASE + 1u));
  assert(!updater_vector_words_are_valid(
      UPDATER_APP_BASE, 0x100u, UPDATER_RAM_BASE + 4u,
      UPDATER_APP_BASE + 1u));
  assert(!updater_vector_words_are_valid(
      UPDATER_APP_BASE, UPDATER_APP_MAX_IMAGE_SIZE + 1u, UPDATER_RAM_END,
      UPDATER_APP_BASE + 1u));
  assert(!updater_vector_words_are_valid(
      UPDATER_APP_BASE + 4u, 0x100u, UPDATER_RAM_END,
      UPDATER_APP_BASE + 5u));
}

static void test_version_anti_rollback(void) {
  assert(updater_version_is_strictly_newer(2, 0, 6, 2, 0, 5));
  assert(updater_version_is_strictly_newer(2, 1, 0, 2, 0, 255));
  assert(updater_version_is_strictly_newer(3, 0, 0, 2, 255, 255));
  assert(!updater_version_is_strictly_newer(2, 0, 5, 2, 0, 5));
  assert(!updater_version_is_strictly_newer(2, 0, 4, 2, 0, 5));
  assert(!updater_version_is_strictly_newer(1, 255, 255, 2, 0, 0));
}

static void test_transport_padding_is_canonical(void) {
  const uint8_t canonical[4] = {0x11u, 0x22u, 0xffu, 0xffu};
  const uint8_t non_erased[4] = {0x11u, 0x22u, 0xffu, 0x00u};
  const uint8_t erased[4] = {0xffu, 0xffu, 0xffu, 0xffu};

  assert(updater_data_padding_is_canonical(10u, 8u, canonical,
                                           sizeof(canonical)));
  assert(!updater_data_padding_is_canonical(10u, 8u, non_erased,
                                            sizeof(non_erased)));
  assert(updater_data_padding_is_canonical(12u, 8u, non_erased,
                                           sizeof(non_erased)));
  assert(updater_data_padding_is_canonical(8u, 8u, erased, sizeof(erased)));
  assert(!updater_data_padding_is_canonical(8u, 8u, non_erased,
                                            sizeof(non_erased)));
  assert(updater_data_padding_is_canonical(0u, 0u, NULL, 0u));
  assert(!updater_data_padding_is_canonical(0u, 0u, NULL, 1u));
  assert(!updater_data_padding_is_canonical(0u, UINT32_MAX, erased, 2u));
}

int main(void) {
  test_checked_alignment();
  test_vector_must_be_inside_declared_image();
  test_vector_ram_and_slot_bounds();
  test_version_anti_rollback();
  test_transport_padding_is_canonical();
  puts("updater_validation_test: ok");
  return 0;
}
