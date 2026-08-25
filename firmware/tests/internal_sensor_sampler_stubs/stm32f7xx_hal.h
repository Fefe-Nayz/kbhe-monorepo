#ifndef STM32F7XX_HAL_INTERNAL_SENSOR_TEST_STUB_H_
#define STM32F7XX_HAL_INTERNAL_SENSOR_TEST_STUB_H_

#include <stdint.h>

typedef struct {
  uint32_t SR;
  uint32_t CR2;
  uint32_t JDR1;
  uint32_t JDR2;
} ADC_TypeDef;

typedef struct {
  uint32_t ownership_cookie;
} DMA_HandleTypeDef;

typedef struct {
  ADC_TypeDef *Instance;
  uint32_t State;
  DMA_HandleTypeDef *DMA_Handle;
} ADC_HandleTypeDef;

#define ADC_CR2_ADON (1u << 0)
#define ADC_CR2_JSWSTART (1u << 22)
#define ADC_FLAG_JEOC (1u << 2)
#define ADC_FLAG_JSTRT (1u << 3)

#define HAL_ADC_STATE_REG_BUSY (1u << 8)
#define HAL_ADC_STATE_TIMEOUT (1u << 2)

#define HAL_IS_BIT_SET(reg, bit) (((reg) & (bit)) == (bit))
#define SET_BIT(reg, bit) ((reg) |= (bit))

#define __HAL_ADC_GET_FLAG(hadc, flag)                                      \
  ((((hadc)->Instance->SR) & (flag)) == (flag))
#define __HAL_ADC_CLEAR_FLAG(hadc, flag)                                    \
  ((hadc)->Instance->SR &= ~(uint32_t)(flag))

#endif /* STM32F7XX_HAL_INTERNAL_SENSOR_TEST_STUB_H_ */
