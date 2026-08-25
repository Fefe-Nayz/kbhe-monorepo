#include "firmware_version.h"
#include "monocypher-ed25519.h"
#include "stm32f7xx_hal.h"
#include "updater_bootloader_version.h"
#include "updater_migration.h"
#include "updater_migrator_plan.h"
#include "updater_shared.h"
#include "updater_version_floor.h"

#include <stddef.h>
#include <string.h>

#define MIGRATOR_PVD_STABLE_SAMPLES 16u

static const uint8_t s_descriptor_magic[8] =
    UPDATER_MIGRATION_DESCRIPTOR_MAGIC_BYTES;
static const uint8_t s_target_id[16] = UPDATER_MIGRATION_TARGET_ID_BYTES;

KBHE_DECLARE_FIRMWARE_VERSION_RECORD(g_kbhe_migrator_version_record);

_Static_assert(sizeof(updater_migration_descriptor_t) ==
                   UPDATER_MIGRATION_DESCRIPTOR_SIZE,
               "migration descriptor layout changed");

void SysTick_Handler(void) { HAL_IncTick(); }

static void migration_fatal(void) {
  __disable_irq();
  while (1) {
    __WFI();
  }
}

static bool version_equal(updater_fw_version_t left,
                          updater_fw_version_t right) {
  return left.major == right.major && left.minor == right.minor &&
         left.patch == right.patch;
}

static bool version_is_older(updater_fw_version_t candidate,
                             updater_fw_version_t installed) {
  if (candidate.major != installed.major) {
    return candidate.major < installed.major;
  }
  if (candidate.minor != installed.minor) {
    return candidate.minor < installed.minor;
  }
  return candidate.patch < installed.patch;
}

static void refresh_flash_cache(uint32_t address, uint32_t length) {
  uint32_t aligned_address = address & ~31u;
  uint32_t end = address + length;
  uint32_t aligned_end = (end + 31u) & ~31u;

  if (length == 0u || end < address) {
    return;
  }
  SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)aligned_address,
                              (int32_t)(aligned_end - aligned_address));
  SCB_InvalidateICache();
  __DSB();
  __ISB();
}

static bool supply_is_stable(void) {
  PWR_PVDTypeDef pvd = {
      .PVDLevel = PWR_PVDLEVEL_6, /* 2.8 V; Flash range 3 starts at 2.7 V. */
      .Mode = PWR_PVD_MODE_NORMAL,
  };

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_ConfigPVD(&pvd);
  HAL_PWR_EnablePVD();
  HAL_Delay(2u);
  for (uint32_t sample = 0u; sample < MIGRATOR_PVD_STABLE_SAMPLES; sample++) {
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO) != RESET) {
      return false;
    }
    HAL_Delay(1u);
  }
  return true;
}

static uint32_t current_bootaddr0(void) {
  FLASH_OBProgramInitTypeDef options = {0};
  HAL_FLASHEx_OBGetConfig(&options);
  return options.BootAddr0;
}

static bool program_bootaddr0(uint32_t bootaddr) {
  FLASH_OBProgramInitTypeDef options = {
      .OptionType = OPTIONBYTE_BOOTADDR_0,
      .BootAddr0 = bootaddr,
  };
  HAL_StatusTypeDef status;

  if (!supply_is_stable()) {
    return false;
  }

  __disable_irq();
  status = HAL_FLASH_OB_Unlock();
  if (status == HAL_OK) {
    status = HAL_FLASHEx_OBProgram(&options);
  }
  if (status == HAL_OK) {
    __DSB();
    status = HAL_FLASH_OB_Launch();
  }
  (void)HAL_FLASH_OB_Lock();
  __enable_irq();
  if (status != HAL_OK || current_bootaddr0() != bootaddr) {
    return false;
  }
  return supply_is_stable();
}

static bool bootloader_vector_is_valid(const uint8_t *image,
                                       uint32_t image_size) {
  uint32_t initial_sp;
  uint32_t reset_handler;
  uint32_t reset_address;

  if (image == NULL || image_size < 8u ||
      image_size > UPDATER_BOOTLOADER_CODE_SIZE) {
    return false;
  }
  memcpy(&initial_sp, image, sizeof(initial_sp));
  memcpy(&reset_handler, image + sizeof(initial_sp), sizeof(reset_handler));
  reset_address = reset_handler & ~1u;
  return initial_sp > UPDATER_RAM_BASE && initial_sp <= UPDATER_RAM_END &&
         (initial_sp & 7u) == 0u && (reset_handler & 1u) != 0u &&
         reset_address >= UPDATER_BOOTLOADER_BASE &&
         reset_address < UPDATER_BOOTLOADER_BASE + image_size;
}

static bool migration_package_read(
    updater_trailer_t *trailer_out,
    updater_migration_descriptor_t *descriptor_out,
    const uint8_t **bootloader_out) {
  updater_trailer_t trailer;
  updater_migration_descriptor_t descriptor;
  const uint8_t *image = (const uint8_t *)(uintptr_t)UPDATER_APP_BASE;
  const uint8_t *descriptor_address;
  const uint8_t *bootloader;
  uint8_t bootloader_sha512[64];
  updater_fw_version_t bootloader_version;
  uint32_t bootloader_end;
  static const uint8_t zeroes[4] = {0};

  if (trailer_out == NULL || descriptor_out == NULL ||
      bootloader_out == NULL || !updater_read_trailer(&trailer) ||
      !updater_is_app_image_valid_with_trailer(&trailer) ||
      trailer.image_size < sizeof(descriptor) ||
      trailer.image_size > UPDATER_APP_MAX_IMAGE_SIZE) {
    return false;
  }
  descriptor_address = image + trailer.image_size - sizeof(descriptor);
  memcpy(&descriptor, descriptor_address, sizeof(descriptor));
  if (memcmp(descriptor.magic, s_descriptor_magic,
             sizeof(descriptor.magic)) != 0 ||
      descriptor.schema != UPDATER_MIGRATION_DESCRIPTOR_SCHEMA ||
      descriptor.descriptor_size != sizeof(descriptor) ||
      descriptor.source_protocol != UPDATER_MIGRATION_SOURCE_PROTOCOL ||
      descriptor.target_protocol != UPDATER_MIGRATION_TARGET_PROTOCOL ||
      descriptor.flags != UPDATER_MIGRATION_REQUIRED_FLAGS ||
      descriptor.image_size != trailer.image_size ||
      memcmp(descriptor.target_id, s_target_id, sizeof(s_target_id)) != 0 ||
      descriptor.bootloader_version_major !=
          UPDATER_BOOTLOADER_VERSION_MAJOR ||
      descriptor.bootloader_version_minor !=
          UPDATER_BOOTLOADER_VERSION_MINOR ||
      descriptor.bootloader_version_patch !=
          UPDATER_BOOTLOADER_VERSION_PATCH ||
      descriptor.reserved0 != 0u ||
      memcmp(descriptor.reserved, zeroes, sizeof(zeroes)) != 0 ||
      descriptor.descriptor_crc32 !=
          updater_crc32_compute(&descriptor,
                                offsetof(updater_migration_descriptor_t,
                                         descriptor_crc32)) ||
      descriptor.bootloader_offset < 8u ||
      descriptor.bootloader_offset >
          UPDATER_MIGRATION_EXECUTABLE_MAX_SIZE ||
      (descriptor.bootloader_offset & 3u) != 0u ||
      descriptor.bootloader_size == 0u ||
      descriptor.bootloader_size > UPDATER_BOOTLOADER_CODE_SIZE ||
      descriptor.bootloader_offset > UINT32_MAX -
                                         descriptor.bootloader_size) {
    return false;
  }
  bootloader_end =
      descriptor.bootloader_offset + descriptor.bootloader_size;
  if (bootloader_end > trailer.image_size - sizeof(descriptor)) {
    return false;
  }
  bootloader = image + descriptor.bootloader_offset;
  if (!bootloader_vector_is_valid(bootloader,
                                  descriptor.bootloader_size) ||
      updater_crc32_compute(bootloader, descriptor.bootloader_size) !=
          descriptor.bootloader_crc32) {
    return false;
  }
  crypto_sha512(bootloader_sha512, bootloader,
                descriptor.bootloader_size);
  if (crypto_verify64(bootloader_sha512,
                      descriptor.bootloader_sha512) != 0) {
    crypto_wipe(bootloader_sha512, sizeof(bootloader_sha512));
    return false;
  }
  crypto_wipe(bootloader_sha512, sizeof(bootloader_sha512));
  if (updater_bootloader_version_scan(
          bootloader, descriptor.bootloader_size,
          &bootloader_version) != UPDATER_BOOTLOADER_VERSION_VALID ||
      bootloader_version.major != descriptor.bootloader_version_major ||
      bootloader_version.minor != descriptor.bootloader_version_minor ||
      bootloader_version.patch != descriptor.bootloader_version_patch) {
    return false;
  }

  *trailer_out = trailer;
  *descriptor_out = descriptor;
  *bootloader_out = bootloader;
  return true;
}

static bool installed_bootloader_matches(
    const updater_migration_descriptor_t *descriptor,
    const uint8_t *bootloader) {
  const uint8_t *installed =
      (const uint8_t *)(uintptr_t)UPDATER_BOOTLOADER_BASE;
  if (descriptor == NULL || bootloader == NULL ||
      descriptor->bootloader_size > UPDATER_BOOTLOADER_CODE_SIZE ||
      memcmp(installed, bootloader, descriptor->bootloader_size) != 0 ||
      updater_crc32_compute(installed, descriptor->bootloader_size) !=
          descriptor->bootloader_crc32) {
    return false;
  }

  /* A cut during the sector erase can otherwise leave legacy instructions
   * after a short, fully programmed new image. Require the entire code region
   * outside the authenticated binary to be erased before advancing the state
   * machine. */
  for (uint32_t offset = descriptor->bootloader_size;
       offset < UPDATER_BOOTLOADER_CODE_SIZE; offset++) {
    if (installed[offset] != 0xFFu) {
      return false;
    }
  }
  return true;
}

static bool erase_sectors(uint32_t first_sector, uint32_t count) {
  FLASH_EraseInitTypeDef erase = {
      .TypeErase = FLASH_TYPEERASE_SECTORS,
      .VoltageRange = FLASH_VOLTAGE_RANGE_3,
      .Sector = first_sector,
      .NbSectors = count,
  };
  uint32_t sector_error = UINT32_MAX;
  bool success;

  if (!supply_is_stable() || HAL_FLASH_Unlock() != HAL_OK) {
    return false;
  }
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                         FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                         FLASH_FLAG_PGPERR | FLASH_FLAG_ERSERR);
  success = HAL_FLASHEx_Erase(&erase, &sector_error) == HAL_OK;
  (void)HAL_FLASH_Lock();
  return success && sector_error == UINT32_MAX && supply_is_stable();
}

static bool install_bootloader(
    const updater_migration_descriptor_t *descriptor,
    const uint8_t *bootloader) {
  bool success = true;

  if (descriptor == NULL || bootloader == NULL ||
      !erase_sectors(FLASH_SECTOR_0, 3u) ||
      HAL_FLASH_Unlock() != HAL_OK) {
    return false;
  }
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                         FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                         FLASH_FLAG_PGPERR | FLASH_FLAG_ERSERR);
  for (uint32_t offset = 0u; offset < descriptor->bootloader_size;
       offset += sizeof(uint32_t)) {
    uint32_t word = UINT32_MAX;
    uint32_t remaining = descriptor->bootloader_size - offset;
    uint32_t copy_length =
        remaining < sizeof(word) ? remaining : sizeof(word);
    memcpy(&word, bootloader + offset, copy_length);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          UPDATER_BOOTLOADER_BASE + offset,
                          (uint64_t)word) != HAL_OK) {
      success = false;
      break;
    }
  }
  (void)HAL_FLASH_Lock();
  refresh_flash_cache(UPDATER_BOOTLOADER_BASE,
                      UPDATER_BOOTLOADER_CODE_SIZE);
  return success && supply_is_stable() &&
         installed_bootloader_matches(descriptor, bootloader);
}

static bool floor_matches(updater_fw_version_t version) {
  updater_fw_version_t installed;
  return updater_version_floor_read(&installed) &&
         version_equal(installed, version) &&
         updater_version_floor_allows(version);
}

static bool install_version_floor(updater_fw_version_t version) {
  if (!erase_sectors(FLASH_SECTOR_3, 1u)) {
    return false;
  }
  refresh_flash_cache(UPDATER_VERSION_FLOOR_BASE,
                      UPDATER_VERSION_FLOOR_SIZE);
  return updater_version_floor_prepare(version) ==
             UPDATER_VERSION_FLOOR_OK &&
         floor_matches(version) && supply_is_stable();
}

static void reset_now(void) {
  __DSB();
  __ISB();
  NVIC_SystemReset();
  migration_fatal();
}

int main(void) {
  updater_trailer_t trailer;
  updater_migration_descriptor_t descriptor;
  const uint8_t *bootloader = NULL;
  updater_fw_version_t version;
  updater_fw_version_t candidate_bootloader_version;
  updater_fw_version_t resident_bootloader_version;
  updater_bootloader_version_scan_t resident_version_state;
  uint32_t bootaddr;
  bool installed;
  bool floor_ready;

  HAL_Init();
  __HAL_RCC_PWR_CLK_ENABLE();
  if (!supply_is_stable() ||
      !migration_package_read(&trailer, &descriptor, &bootloader)) {
    migration_fatal();
  }
  version.major = trailer.fw_version_major;
  version.minor = trailer.fw_version_minor;
  version.patch = trailer.fw_version_patch;
  if (version.major != FIRMWARE_VERSION_MAJOR ||
      version.minor != FIRMWARE_VERSION_MINOR ||
      version.patch != FIRMWARE_VERSION_PATCH) {
    migration_fatal();
  }
  candidate_bootloader_version.major = descriptor.bootloader_version_major;
  candidate_bootloader_version.minor = descriptor.bootloader_version_minor;
  candidate_bootloader_version.patch = descriptor.bootloader_version_patch;
  resident_version_state = updater_bootloader_version_scan(
      (const void *)(uintptr_t)UPDATER_BOOTLOADER_BASE,
      UPDATER_BOOTLOADER_CODE_SIZE, &resident_bootloader_version);
  /* Legacy or torn residents have no valid KBLV and remain recoverable. A
   * coherent resident, however, may never be replaced by an older signed
   * updater even when a non-official host replays the refresh image. */
  if (resident_version_state == UPDATER_BOOTLOADER_VERSION_AMBIGUOUS ||
      (resident_version_state == UPDATER_BOOTLOADER_VERSION_VALID &&
       version_is_older(candidate_bootloader_version,
                        resident_bootloader_version))) {
    migration_fatal();
  }

  bootaddr = current_bootaddr0();
  installed = installed_bootloader_matches(&descriptor, bootloader);
  floor_ready = floor_matches(version);

  while (1) {
    switch (updater_migrator_plan_next(bootaddr, installed, floor_ready)) {
    case UPDATER_MIGRATOR_ACTION_ENTER_UPDATER:
      boot_request_set(BOOT_REQUEST_ACTION_ENTER_UPDATER);
      reset_now();
      break;
    case UPDATER_MIGRATOR_ACTION_SET_BOOT_APP:
      boot_request_clear();
      if (!program_bootaddr0(UPDATER_MIGRATOR_BOOTADDR_AXIM_APP)) {
        migration_fatal();
      }
      reset_now();
      break;
    case UPDATER_MIGRATOR_ACTION_INSTALL_BOOTLOADER:
      if (!install_bootloader(&descriptor, bootloader)) {
        migration_fatal();
      }
      installed = true;
      break;
    case UPDATER_MIGRATOR_ACTION_INSTALL_VERSION_FLOOR:
      if (!install_version_floor(version)) {
        migration_fatal();
      }
      floor_ready = true;
      break;
    case UPDATER_MIGRATOR_ACTION_SET_BOOT_UPDATER:
      if (!installed_bootloader_matches(&descriptor, bootloader) ||
          !floor_matches(version) ||
          !updater_is_app_image_valid_with_trailer(&trailer)) {
        migration_fatal();
      }
      boot_request_set(BOOT_REQUEST_ACTION_ENTER_UPDATER);
      if (!program_bootaddr0(UPDATER_MIGRATOR_BOOTADDR_AXIM_FLASH)) {
        migration_fatal();
      }
      reset_now();
      break;
    case UPDATER_MIGRATOR_ACTION_FAIL_CLOSED:
    default:
      migration_fatal();
      break;
    }
  }
}
