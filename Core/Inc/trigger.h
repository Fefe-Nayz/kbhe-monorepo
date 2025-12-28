/*
 * trigger.h
 * API pour le trigger rapide des touches analogiques
 */

#ifndef TRIGGER_H_
#define TRIGGER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handle trigger logic for a single key
 * @param keyIndex Index of the key (0-5)
 * @param currentVoltage Current ADC voltage reading
 */
void handleTrigger(int keyIndex, int currentVoltage);

/**
 * Get current key state
 * @param keyIndex Index of the key (0-5)
 * @return 1 if pressed, 0 if released
 */
int getKeyState(int keyIndex);

#ifdef __cplusplus
}
#endif

#endif /* TRIGGER_H_ */
