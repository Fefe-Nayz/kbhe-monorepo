#include "trigger.h"
#include "lut.c"
#include "offset.c"
#include "usb_hid.h"
#include <stdint.h>

/**
 * DISABLE KEYBOARD TYPING
 */
#define DISABLE_KEYBOARD_TYPING 0

// Mapping des touches vers les keycodes HID
static const uint8_t KEY_HID_CODES[6] = {
    HID_KEY_Q, // Key 0
    HID_KEY_W, // Key 1
    HID_KEY_E, // Key 2
    HID_KEY_A, // Key 3
    HID_KEY_S, // Key 4
    HID_KEY_D, // Key 5
};

// Lorsque l'appuie est sous le point d'activation et que la variation est
// supérieure au delta alors on déclenche un trigger rapide si la touche
// s'enfonce ou un release rapide remonte
float RAPID_TRIGGER_DELTA = 0.5;

// Point d'activation/désactivation de la touche
float ACTUATION_POINT[6] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5};

// Valeur maximale d'appuie atteinte lorsque la touche est pressée puis en cours
// de relachement
float maxBottomDistances[6] = {0, 0, 0, 0, 0, 0};
// Valeur minimale d'appuie atteinte lorsque la touche est relachée puis en
// cours d'appuie
float minTopDistances[6] = {0, 0, 0, 0, 0, 0};

// Dernière valeur d'appuie lue
float distances[6] = {0, 0, 0, 0, 0, 0};

// Dernier état de la touche (0 = relachée, 1 = appuyée)
int states[6] = {0, 0, 0, 0, 0, 0};

int socdKeyMapping[6] = {-1, -1, -1, 5, -1, 3};
// Si l'état de la touche est override par le SOCD
int socdOverrideState[6] = {0, 0, 0, 0, 0, 0};

void triggerInit() { offsetInit(); }

int getKeyState(int keyIndex) {
  if (keyIndex < 0 || keyIndex >= 6)
    return 0;
  return states[keyIndex];
}

void updateKeyData(int keyIndex, float currentDistance, int resetExtremums) {
  if (resetExtremums) {
    // Réinitialisation des points extrêmes
    minTopDistances[keyIndex] = currentDistance;
    maxBottomDistances[keyIndex] = currentDistance;
  } else {
    // Mise à jour des points extrêmes
    // Si on remonte (distance diminue), on met à jour le minimum
    if (currentDistance < minTopDistances[keyIndex]) {
      minTopDistances[keyIndex] = currentDistance;
    }

    // Si on descend (distance augmente), on met à jour le maximum
    if (currentDistance > maxBottomDistances[keyIndex]) {
      maxBottomDistances[keyIndex] = currentDistance;
    }
  }

  // Mise à jour de la dernière distance (à la fin!)
  distances[keyIndex] = currentDistance;
}

void handleSOCDOnRelease(int keyIndex) {
  // Récuperer la touche liée à la touche relachée
  int socdMappedKey = socdKeyMapping[keyIndex];

  // Si le SOCD est actif
  if (socdMappedKey == -1)
    return;

  // Si la touche liée était override par le SOCD on la remet dans son état
  // normal
  int mappedKeyState = states[socdMappedKey];
  int mappedKeyOverrideState = socdOverrideState[socdMappedKey];

  if (mappedKeyState == 1 && mappedKeyOverrideState == 1) {
    if (DISABLE_KEYBOARD_TYPING) {
      return;
    }

    usb_hid_key_press(KEY_HID_CODES[socdMappedKey]);
  }

  // Désactiver l'override SOCD
  socdOverrideState[socdMappedKey] = 0;
}

void handleSOCDOnPress(int keyIndex) {
  // Récuperer la touche liée à la touche pressée
  int socdMappedKey = socdKeyMapping[keyIndex];

  // Si le SOCD est actif
  if (socdMappedKey == -1)
    return;

  // Si la touche liée est déjà relachée, on ne fait rien
  int mappedKeyState = states[socdMappedKey];

  if (mappedKeyState == 0) {
    return;
  }

  // Relacher la touche liée par override SOCD
  socdOverrideState[socdMappedKey] = 1;

  if (DISABLE_KEYBOARD_TYPING) {
    return;
  }

  usb_hid_key_release(KEY_HID_CODES[socdMappedKey]);
}

void release(int keyIndex, int rapid) {
  if (keyIndex < 0 || keyIndex >= 6)
    return;

  if (states[keyIndex] == 0) {
    return;
  }

  states[keyIndex] = 0;

  if (DISABLE_KEYBOARD_TYPING) {
    return;
  }

  // Gérer le SOCD
  handleSOCDOnRelease(keyIndex);

  usb_hid_key_release(KEY_HID_CODES[keyIndex]);
}

void press(int keyIndex, int rapid) {
  if (keyIndex < 0 || keyIndex >= 6)
    return;

  // Si la touche est déjà pressée, on ne fait rien
  if (states[keyIndex] == 1) {
    return;
  }

  // Mettre à jour l'état de la touche
  states[keyIndex] = 1;

  // Si la saisie clavier est désactivée, on ne fait rien de plus
  if (DISABLE_KEYBOARD_TYPING) {
    return;
  }

  // Gérer le SOCD
  handleSOCDOnPress(keyIndex);

  // Envoyer l'événement de pression de touche via USB HID
  usb_hid_key_press(KEY_HID_CODES[keyIndex]);
}

void handleTrigger(int keyIndex, int currentVoltage) {
  if (keyIndex < 0 || keyIndex >= 6)
    return;

  float correctedCurrentVoltage = getCorrectedValue(keyIndex, currentVoltage);
  float currentDistance = getValueFromLUT(correctedCurrentVoltage);

  float lastDistance = distances[keyIndex];

  if (lastDistance == currentDistance) {
    return;
  }

  float actuationPoint = ACTUATION_POINT[keyIndex];
  int lastState = states[keyIndex];

  /**
   * NORMAL RELEASE DETECTION
   */

  // Si la touche est au dessus du point d'activation
  if (currentDistance < actuationPoint) {
    // Normal release
    updateKeyData(keyIndex, currentDistance, 1);
    release(keyIndex, 0);
    return;
  }

  // Si la touche est en descente
  if (lastDistance < currentDistance) {
    /**
     * NORMAL PRESS DETECTION
     */

    // Si on passe sous le point d'activation
    if (currentDistance >= actuationPoint && lastDistance < actuationPoint) {
      // Normal press
      updateKeyData(keyIndex, currentDistance, 1);
      press(keyIndex, 0);
      return;
    }

    /**
     * RAPID PRESS DETECTION
     */

    // Si on est déjà sous le point d'activation
    // Et que la touche est relachée
    if (lastState == 0) {
      float minTopDistance = minTopDistances[keyIndex];

      // Si on est descendu de plus que le delta depuis le point le plus haut
      // atteint
      if (currentDistance - minTopDistance >= RAPID_TRIGGER_DELTA) {
        // Rapid press
        updateKeyData(keyIndex, currentDistance, 1);
        press(keyIndex, 1);
        return;
      }
    }
  }

  // Si la touche est en montée
  if (lastDistance > currentDistance) {
    /**
     * RAPID RELEASE DETECTION
     */

    // Si la touche est déjà pressée
    if (lastState == 1) {
      float maxBottomDistance = maxBottomDistances[keyIndex];

      // Si on est remonté de plus que le delta depuis le point le plus bas
      // atteint
      if (maxBottomDistance - currentDistance >= RAPID_TRIGGER_DELTA) {
        // Rapid release
        updateKeyData(keyIndex, currentDistance, 1);
        release(keyIndex, 1);
        return;
      }
    }
  }

  updateKeyData(keyIndex, currentDistance, 0);
}