#ifndef ROTARY_ENCODER_H_
#define ROTARY_ENCODER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rotary_encoder_init(void);
void rotary_encoder_task(uint32_t now_ms);
bool rotary_encoder_is_button_pressed(void);
int8_t rotary_encoder_get_last_direction(void);
uint32_t rotary_encoder_get_step_counter(void);

#ifdef __cplusplus
}
#endif

#endif /* ROTARY_ENCODER_H_ */
