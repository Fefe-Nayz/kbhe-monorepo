#ifndef ROTARY_ENCODER_H_
#define ROTARY_ENCODER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rotary_encoder_init(void);
void rotary_encoder_task(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* ROTARY_ENCODER_H_ */
