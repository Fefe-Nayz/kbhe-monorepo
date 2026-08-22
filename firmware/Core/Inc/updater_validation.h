#ifndef UPDATER_VALIDATION_H_
#define UPDATER_VALIDATION_H_

#include <stdbool.h>
#include <stdint.h>

/** HAL-independent vector validation used by firmware and host tests. */
bool updater_vector_words_are_valid(uint32_t app_base, uint32_t image_size,
                                    uint32_t initial_sp,
                                    uint32_t reset_handler);

/** Require transport padding after image_size to remain erased (0xFF). */
bool updater_data_padding_is_canonical(uint32_t image_size, uint32_t offset,
                                       const uint8_t *data,
                                       uint32_t data_length);

/** Lexicographic stable-version comparison used for anti-rollback checks. */
bool updater_version_is_strictly_newer(uint8_t candidate_major,
                                       uint8_t candidate_minor,
                                       uint8_t candidate_patch,
                                       uint8_t installed_major,
                                       uint8_t installed_minor,
                                       uint8_t installed_patch);

#endif /* UPDATER_VALIDATION_H_ */
