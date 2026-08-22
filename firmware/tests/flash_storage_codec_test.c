#include "flash_storage_codec.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_commit_is_last_word_and_required(void) {
  uint8_t payload[7] = {1u, 2u, 3u, 4u, 5u, 6u, 7u};
  flash_storage_object_header_t header;
  flash_storage_commit_t commit = {0};

  flash_storage_codec_prepare_header(&header, 5u, 2u, 9u, payload,
                                     sizeof(payload), 42u);
  assert(flash_storage_codec_header_is_valid(&header));
  commit.generation = 42u;
  commit.magic = UINT32_MAX; /* erased/torn commit marker */
  assert(!flash_storage_codec_record_is_committed(&header, payload, &commit));
  commit.magic = FLASH_STORAGE_COMMIT_MAGIC;
  assert(flash_storage_codec_record_is_committed(&header, payload, &commit));
}

static void test_corruption_is_rejected(void) {
  uint8_t payload[4] = {1u, 2u, 3u, 4u};
  flash_storage_object_header_t header;
  flash_storage_commit_t commit = {
      .generation = 7u,
      .magic = FLASH_STORAGE_COMMIT_MAGIC,
  };

  flash_storage_codec_prepare_header(&header, 1u, 0u, 30u, payload,
                                     sizeof(payload), 7u);
  payload[2] ^= 0x55u;
  assert(!flash_storage_codec_record_is_committed(&header, payload, &commit));
  payload[2] ^= 0x55u;
  header.schema_version++;
  assert(!flash_storage_codec_header_is_valid(&header));
}

static void test_record_size_is_checked_and_aligned(void) {
  uint32_t payload_size = 0u;
  uint32_t record_size = 0u;
  assert(flash_storage_codec_record_size(7u, &payload_size, &record_size));
  assert(payload_size == 8u);
  assert(record_size == sizeof(flash_storage_object_header_t) + 8u +
                            sizeof(flash_storage_commit_t));
  assert(!flash_storage_codec_record_size(0u, &payload_size, &record_size));
  assert(!flash_storage_codec_record_size(UINT32_MAX, &payload_size,
                                          &record_size));
}

static void test_segmented_crc_matches_contiguous_payload(void) {
  const uint8_t payload[] = {9u, 8u, 7u, 6u, 5u, 4u};
  uint32_t state = 0xFFFFFFFFu;
  state = flash_storage_codec_crc32_update(state, payload, 2u);
  state = flash_storage_codec_crc32_update(state, payload + 2u, 4u);
  assert(~state == flash_storage_codec_crc32(payload, sizeof(payload)));
}

static void test_bank_commit_selects_only_complete_new_generation(void) {
  flash_storage_bank_header_t old_bank;
  flash_storage_bank_header_t new_bank;
  uint8_t selected = 0xFFu;

  flash_storage_codec_prepare_bank_header(&old_bank, 9u, true);
  flash_storage_codec_prepare_bank_header(&new_bank, 10u, false);
  assert(flash_storage_codec_select_bank(&old_bank, &new_bank, &selected));
  assert(selected == 0u); /* simulated power cut before final bank commit */

  new_bank.commit_magic = FLASH_STORAGE_BANK_COMMIT_MAGIC;
  assert(flash_storage_codec_select_bank(&old_bank, &new_bank, &selected));
  assert(selected == 1u);

  new_bank.header_crc32 ^= 1u;
  assert(flash_storage_codec_select_bank(&old_bank, &new_bank, &selected));
  assert(selected == 0u);
}

static void test_bank_generation_wrap_is_ordered(void) {
  flash_storage_bank_header_t old_bank;
  flash_storage_bank_header_t new_bank;
  uint8_t selected = 0xFFu;

  flash_storage_codec_prepare_bank_header(&old_bank, UINT32_MAX, true);
  flash_storage_codec_prepare_bank_header(&new_bank, 1u, true);
  assert(flash_storage_codec_select_bank(&old_bank, &new_bank, &selected));
  assert(selected == 1u);
}

int main(void) {
  test_commit_is_last_word_and_required();
  test_corruption_is_rejected();
  test_record_size_is_checked_and_aligned();
  test_segmented_crc_matches_contiguous_payload();
  test_bank_commit_selects_only_complete_new_generation();
  test_bank_generation_wrap_is_ordered();
  puts("flash_storage_codec_test: ok");
  return 0;
}
