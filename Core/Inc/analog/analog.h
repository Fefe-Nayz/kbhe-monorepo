#include <stdbool.h>
#include <stdint.h>
#include "main.h"

#define ADC_BUFFER_LENGTH 128

// // Number of keys
// #define NUM_KEYS 82

// // Number of multiplexers
// #define NUM_MUX 11
// // Number of channels per multiplexer
// #define NUM_MUX_CHANNELS 8

// Number of keys
#define NUM_KEYS 6

// Number of multiplexers
#define NUM_MUX 8
// Number of channels per multiplexer
#define NUM_MUX_CHANNELS 13

// Number of analog inputs
#define NUM_ANALOG_INPUTS (NUM_MUX * NUM_MUX_CHANNELS)

// Analog configuration structure
typedef struct {
    // ADC
    ADC_HandleTypeDef* hadc;
} AnalogConfig_t;

void analog_init(AnalogConfig_t* config);

void analog_set_multiplexer_channel(uint8_t channel);

bool analog_is_scan_complete(void);

void analog_set_scan_complete(bool complete);

uint16_t analog_read_raw_value(uint8_t key);

uint16_t* analog_get_adc_buffer_ptr(void);