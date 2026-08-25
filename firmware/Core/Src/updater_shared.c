#include "updater_shared.h"
#include "firmware_public_key.h"
#include "monocypher-ed25519.h"
#include "updater_migration.h"
#include "updater_validation.h"
#include "stm32f7xx.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(updater_signature_manifest_t) ==
                   UPDATER_SIGNATURE_MANIFEST_SIZE,
               "firmware signature manifest layout changed");
_Static_assert(sizeof(updater_packet_t) == UPDATER_PACKET_SIZE,
               "updater packet layout changed");
_Static_assert(sizeof(updater_trailer_t) == 84u,
               "signed updater trailer layout changed");
_Static_assert(sizeof(updater_trailer_t) <= UPDATER_TRAILER_RESERVED_SIZE,
               "updater trailer exceeds reserved flash space");
_Static_assert(sizeof(updater_begin_request_t) == 12u,
               "updater BEGIN layout changed");
_Static_assert(sizeof(updater_hello_payload_t) == 20u,
               "updater HELLO layout changed");
_Static_assert(sizeof(updater_progress_payload_t) == 16u,
               "updater progress layout changed");

static const uint8_t s_manifest_context[8] = {'K', 'B', 'H', 'E',
                                               'F', 'W', '3', 0};
static const uint8_t s_migration_descriptor_magic[8] =
    UPDATER_MIGRATION_DESCRIPTOR_MAGIC_BYTES;
static const uint8_t s_migration_target_id[16] =
    UPDATER_MIGRATION_TARGET_ID_BYTES;

#if defined(UPDATER_HOST_TEST)
/* Supplied by the host updater harness. Keeping the translation behind a
 * test-only guard lets the production build continue to use the MCU's
 * memory-mapped flash addresses verbatim. */
extern const void *updater_host_flash_address(uint32_t address, uint32_t len);
#endif

static const void *updater_flash_address(uint32_t address, uint32_t len) {
#if defined(UPDATER_HOST_TEST)
  return updater_host_flash_address(address, len);
#else
  (void)len;
  return (const void *)(uintptr_t)address;
#endif
}

volatile boot_request_t g_boot_request
    __attribute__((section(".boot_shared"), aligned(4), used));

void boot_request_clear(void) {
  g_boot_request.magic = 0u;
  g_boot_request.action = BOOT_REQUEST_ACTION_NONE;
  __DSB();
  __ISB();
}

void boot_request_set(boot_request_action_t action) {
  g_boot_request.magic = UPDATER_BOOT_REQUEST_MAGIC;
  g_boot_request.action = (uint32_t)action;
  __DSB();
  __ISB();
}

bool boot_request_take(boot_request_action_t action) {
  bool matched = (g_boot_request.magic == UPDATER_BOOT_REQUEST_MAGIC) &&
                 (g_boot_request.action == (uint32_t)action);
  if (matched) {
    boot_request_clear();
  }

  return matched;
}

uint32_t updater_crc32_compute(const void *data, uint32_t len) {
  const uint8_t *buf = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;

  while (len--) {
    crc ^= *buf++;
    for (uint32_t i = 0; i < 8u; i++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }

  return ~crc;
}

void updater_signature_manifest_prepare(
    updater_signature_manifest_t *manifest, const void *image,
    uint32_t image_size, uint32_t image_crc32,
    updater_fw_version_t fw_version) {
  if (manifest == NULL || image == NULL) {
    return;
  }
  memset(manifest, 0, sizeof(*manifest));
  memcpy(manifest->context, s_manifest_context, sizeof(manifest->context));
  manifest->image_size = image_size;
  manifest->image_crc32 = image_crc32;
  manifest->fw_version_major = fw_version.major;
  manifest->fw_version_minor = fw_version.minor;
  manifest->fw_version_patch = fw_version.patch;
  crypto_sha512(manifest->image_sha512, (const uint8_t *)image, image_size);
}

bool updater_signature_manifest_is_sane(
    const updater_signature_manifest_t *manifest) {
  return manifest != NULL &&
         memcmp(manifest->context, s_manifest_context,
                sizeof(manifest->context)) == 0 &&
         manifest->image_size >= 8u &&
         manifest->image_size <= UPDATER_APP_MAX_IMAGE_SIZE &&
         manifest->reserved == 0u;
}

bool updater_signature_manifest_verify(
    const updater_signature_manifest_t *manifest,
    const uint8_t signature[UPDATER_SIGNATURE_SIZE]) {
  if (!updater_signature_manifest_is_sane(manifest) || signature == NULL) {
    return false;
  }
  return crypto_ed25519_check(signature, KBHE_FIRMWARE_RELEASE_PUBLIC_KEY,
                              (const uint8_t *)manifest,
                              sizeof(*manifest)) == 0;
}

void updater_trailer_prepare(
    updater_trailer_t *trailer, uint32_t image_size, uint32_t image_crc32,
    updater_fw_version_t fw_version,
    const uint8_t signature[UPDATER_SIGNATURE_SIZE]) {
  if (trailer == NULL) {
    return;
  }
  memset(trailer, 0, sizeof(*trailer));
  trailer->magic = UPDATER_TRAILER_MAGIC;
  trailer->image_size = image_size;
  trailer->image_crc32 = image_crc32;
  trailer->fw_version_major = fw_version.major;
  trailer->fw_version_minor = fw_version.minor;
  trailer->fw_version_patch = fw_version.patch;
  if (signature != NULL) {
    memcpy(trailer->signature, signature, sizeof(trailer->signature));
  }
  trailer->trailer_crc32 =
      updater_crc32_compute(trailer, sizeof(*trailer) - sizeof(uint32_t));
}

static bool updater_trailer_is_sane(const updater_trailer_t *trailer) {
  if (trailer == NULL || trailer->magic != UPDATER_TRAILER_MAGIC ||
      trailer->reserved != 0u) {
    return false;
  }

  if (trailer->image_size == 0u ||
      trailer->image_size > UPDATER_APP_MAX_IMAGE_SIZE) {
    return false;
  }

  return true;
}

bool updater_trailer_is_valid(const updater_trailer_t *trailer) {
  uint32_t computed_crc;

  if (!updater_trailer_is_sane(trailer)) {
    return false;
  }

  computed_crc =
      updater_crc32_compute(trailer, sizeof(*trailer) - sizeof(uint32_t));
  return computed_crc == trailer->trailer_crc32;
}

bool updater_read_trailer(updater_trailer_t *out_trailer) {
  const updater_trailer_t *flash_trailer;

  if (out_trailer == NULL) {
    return false;
  }
  flash_trailer = (const updater_trailer_t *)updater_flash_address(
      UPDATER_TRAILER_ADDR, sizeof(*flash_trailer));
  if (flash_trailer == NULL) {
    return false;
  }

  memcpy(out_trailer, flash_trailer, sizeof(*out_trailer));
  return updater_trailer_is_valid(out_trailer);
}

bool updater_is_app_vector_valid_for_image(uint32_t app_base,
                                           uint32_t image_size) {
  const uint32_t *vector;
  if (app_base != UPDATER_APP_BASE || image_size < 8u ||
      image_size > UPDATER_APP_MAX_IMAGE_SIZE) {
    return false;
  }
  vector = (const uint32_t *)updater_flash_address(app_base, 8u);
  if (vector == NULL) {
    return false;
  }
  return updater_vector_words_are_valid(app_base, image_size, vector[0],
                                        vector[1]);
}

bool updater_is_app_vector_valid(uint32_t app_base) {
  return updater_is_app_vector_valid_for_image(app_base,
                                                UPDATER_APP_MAX_IMAGE_SIZE);
}

bool updater_is_app_image_valid_with_trailer(
    const updater_trailer_t *trailer) {
  updater_signature_manifest_t manifest;
  const void *image;
  uint32_t computed_crc;

  if (!updater_trailer_is_valid(trailer)) {
    return false;
  }

  if (!updater_is_app_vector_valid_for_image(UPDATER_APP_BASE,
                                              trailer->image_size)) {
    return false;
  }

  image = updater_flash_address(UPDATER_APP_BASE, trailer->image_size);
  if (image == NULL) {
    return false;
  }
  computed_crc = updater_crc32_compute(image, trailer->image_size);
  if (computed_crc != trailer->image_crc32) {
    return false;
  }

  updater_fw_version_t version = {
      .major = trailer->fw_version_major,
      .minor = trailer->fw_version_minor,
      .patch = trailer->fw_version_patch,
  };
  updater_signature_manifest_prepare(&manifest, image, trailer->image_size,
                                     trailer->image_crc32, version);
  bool authenticated =
      updater_signature_manifest_verify(&manifest, trailer->signature);
  crypto_wipe(&manifest, sizeof(manifest));
  return authenticated;
}

bool updater_app_has_valid_migration_descriptor(
    const updater_trailer_t *trailer) {
  updater_migration_descriptor_t descriptor;
  const uint8_t *image;
  const uint8_t *bootloader;
  uint8_t bootloader_sha512[64];
  uint32_t bootloader_end;
  uint32_t initial_sp;
  uint32_t reset_handler;
  uint32_t reset_address;
  static const uint8_t zeroes[8] = {0};

  if (!updater_trailer_is_valid(trailer) ||
      trailer->image_size < sizeof(descriptor)) {
    return false;
  }
  image = updater_flash_address(UPDATER_APP_BASE, trailer->image_size);
  if (image == NULL) {
    return false;
  }
  memcpy(&descriptor, image + trailer->image_size - sizeof(descriptor),
         sizeof(descriptor));
  if (memcmp(descriptor.magic, s_migration_descriptor_magic,
             sizeof(descriptor.magic)) != 0 ||
      descriptor.schema != UPDATER_MIGRATION_DESCRIPTOR_SCHEMA ||
      descriptor.descriptor_size != sizeof(descriptor) ||
      descriptor.source_protocol != UPDATER_MIGRATION_SOURCE_PROTOCOL ||
      descriptor.target_protocol != UPDATER_MIGRATION_TARGET_PROTOCOL ||
      descriptor.flags != UPDATER_MIGRATION_REQUIRED_FLAGS ||
      descriptor.image_size != trailer->image_size ||
      memcmp(descriptor.target_id, s_migration_target_id,
             sizeof(descriptor.target_id)) != 0 ||
      memcmp(descriptor.reserved, zeroes, sizeof(descriptor.reserved)) != 0 ||
      descriptor.descriptor_crc32 !=
          updater_crc32_compute(&descriptor,
                                offsetof(updater_migration_descriptor_t,
                                         descriptor_crc32)) ||
      descriptor.bootloader_offset < 8u ||
      descriptor.bootloader_offset >
          UPDATER_MIGRATION_EXECUTABLE_MAX_SIZE ||
      (descriptor.bootloader_offset & 3u) != 0u ||
      descriptor.bootloader_size < 8u ||
      descriptor.bootloader_size > UPDATER_BOOTLOADER_CODE_SIZE ||
      descriptor.bootloader_offset > UINT32_MAX - descriptor.bootloader_size) {
    return false;
  }
  bootloader_end = descriptor.bootloader_offset + descriptor.bootloader_size;
  if (bootloader_end > trailer->image_size - sizeof(descriptor)) {
    return false;
  }
  bootloader = image + descriptor.bootloader_offset;
  memcpy(&initial_sp, bootloader, sizeof(initial_sp));
  memcpy(&reset_handler, bootloader + sizeof(initial_sp),
         sizeof(reset_handler));
  reset_address = reset_handler & ~1u;
  if (initial_sp <= UPDATER_RAM_BASE || initial_sp > UPDATER_RAM_END ||
      (initial_sp & 7u) != 0u || (reset_handler & 1u) == 0u ||
      reset_address < UPDATER_BOOTLOADER_BASE ||
      reset_address >= UPDATER_BOOTLOADER_BASE + descriptor.bootloader_size ||
      updater_crc32_compute(bootloader, descriptor.bootloader_size) !=
          descriptor.bootloader_crc32) {
    return false;
  }
  crypto_sha512(bootloader_sha512, bootloader, descriptor.bootloader_size);
  bool authenticated_descriptor =
      crypto_verify64(bootloader_sha512, descriptor.bootloader_sha512) == 0;
  crypto_wipe(bootloader_sha512, sizeof(bootloader_sha512));
  return authenticated_descriptor;
}

bool updater_is_app_image_valid(void) {
  updater_fw_version_t ignored;
  return updater_read_valid_app_version(&ignored);
}

bool updater_read_valid_app_version(updater_fw_version_t *version_out) {
  updater_trailer_t trailer;

  if (version_out == NULL || !updater_read_trailer(&trailer) ||
      !updater_is_app_image_valid_with_trailer(&trailer)) {
    return false;
  }

  version_out->major = trailer.fw_version_major;
  version_out->minor = trailer.fw_version_minor;
  version_out->patch = trailer.fw_version_patch;
  return true;
}

updater_fw_version_t updater_get_app_version(void) {
  updater_fw_version_t version = {0};
  (void)updater_read_valid_app_version(&version);
  return version;
}
