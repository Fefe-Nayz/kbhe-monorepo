#include <stdint.h>

/*
 * Number of points in the LUT
 */
// #define LUT_SIZE 572
#define LUT_SIZE 671

/*
 * Starting voltage value for the LUT (in ADC points)
 * This means that LUT[0] corresponds to a voltage of 2100 pts, LUT[1] to 2101 pts, etc.
 */
// #define LUT_BASE_VOLTAGE 2100
#define LUT_BASE_VOLTAGE 2180

int16_t getDistanceFromVoltage(uint16_t voltage); 

float getValueFromLUT(int voltage);