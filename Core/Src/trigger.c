#include "trigger.h"
#include "led_matrix.h"
#include "lut.c"
#include "offset.c"
#include "settings.h"
#include "usb_gamepad.h"
#include "usb_hid.h"
#include "usb_hid_nkro.h"
#include <stdint.h>

/**
 * DISABLE KEYBOARD TYPING (compile-time default, overridden by settings)
 * Set to 1 to disable keyboard HID output by default
 */
#define DISABLE_KEYBOARD_TYPING 0

/**
 * DISABLE GAMEPAD OUTPUT (compile-time default, overridden by settings)
 * Set to 1 to disable gamepad HID output by default
 */
#define DISABLE_GAMEPAD_OUTPUT 0

// Default HID keycodes (fallback if settings not loaded)
static const uint8_t DEFAULT_KEY_HID_CODES[6] = {
    HID_KEY_Q, // Key 0
    HID_KEY_W, // Key 1
    HID_KEY_E, // Key 2
    HID_KEY_A, // Key 3
    HID_KEY_S, // Key 4
    HID_KEY_D, // Key 5
};

// Default rapid trigger settings
#define DEFAULT_RAPID_TRIGGER_DELTA 0.3f    // 0.3mm default sensitivity
#define DEFAULT_RAPID_TRIGGER_ACTIVATION 0.5f // 0.5mm initial activation

// Default actuation point (1.2mm)
#define DEFAULT_ACTUATION_POINT 1.2f

// Default release point (1.2mm)
#define DEFAULT_RELEASE_POINT 1.2f

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

// Whether the key is currently overridden by SOCD
static int socdOverrideState[6] = {0, 0, 0, 0, 0, 0};

/**
 * @brief Get HID keycode for a key from settings
 */
static uint8_t getKeyHIDCode(int keyIndex) {
  const settings_t *s = settings_get();
  if (s && s->keys[keyIndex].hid_keycode != 0) {
    return s->keys[keyIndex].hid_keycode;
  }
  return DEFAULT_KEY_HID_CODES[keyIndex];
}

/**
 * @brief Get actuation point in mm for a key from settings
 */
static float getActuationPoint(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return s->keys[keyIndex].actuation_point_mm / 10.0f; // Convert from 0.1mm
  }
  return DEFAULT_ACTUATION_POINT;
}

/**
 * @brief Get release point in mm for a key from settings
 */
static float getReleasePoint(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return s->keys[keyIndex].release_point_mm / 10.0f; // Convert from 0.1mm
  }
  return DEFAULT_RELEASE_POINT;
}

/**
 * @brief Check if rapid trigger is enabled for this key
 */
static int isRapidTriggerEnabled(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return s->keys[keyIndex].rapid_trigger_enabled;
  }
  return 0; // Disabled by default
}

/**
 * @brief Get rapid trigger initial activation distance in mm
 */
static float getRapidTriggerActivation(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return s->keys[keyIndex].rapid_trigger_activation / 10.0f;  // Convert from 0.1mm
  }
  return DEFAULT_RAPID_TRIGGER_ACTIVATION;
}

/**
 * @brief Get rapid trigger press sensitivity in mm
 */
static float getRapidTriggerPressSensitivity(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return s->keys[keyIndex].rapid_trigger_press /
           100.0f; // Convert from 0.01mm
  }
  return DEFAULT_RAPID_TRIGGER_DELTA;
}

static float getRapidTriggerReleaseSensitivity(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return s->keys[keyIndex].rapid_trigger_release /
           100.0f; // Convert from 0.01mm
  }
  return DEFAULT_RAPID_TRIGGER_DELTA;
}

/**
 * @brief Get SOCD paired key index from settings (-1 if none)
 */
static int getSOCDPairedKey(int keyIndex) {
  const settings_t *s = settings_get();
  if (s && s->keys[keyIndex].socd_pair < 6) {
    return s->keys[keyIndex].socd_pair;
  }
  return -1; // No SOCD pair
}

void triggerInit() {
  offsetInit();
  usb_gamepad_init();

  // Apply compile-time flags
  usb_gamepad_set_enabled(!DISABLE_GAMEPAD_OUTPUT);
}

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

/**
 * SOCD (Simultaneous Opposing Cardinal Directions) handling
 * When a key is released, restore the opposing key if it was overridden
 */
static void handleSOCDOnRelease(int keyIndex) {
  int socdMappedKey = getSOCDPairedKey(keyIndex);

  // No SOCD mapping for this key
  if (socdMappedKey == -1)
    return;

  // If the opposing key was overridden and is still physically pressed,
  // re-send the key press
  int mappedKeyState = states[socdMappedKey];
  int mappedKeyOverrideState = socdOverrideState[socdMappedKey];

  if (mappedKeyState == 1 && mappedKeyOverrideState == 1) {
    if (settings_is_keyboard_enabled()) {
      if (settings_is_nkro_enabled()) {
        usb_hid_nkro_key_press(getKeyHIDCode(socdMappedKey));
      } else {
        usb_hid_key_press(getKeyHIDCode(socdMappedKey));
      }
    }
  }

  // Clear the override state
  socdOverrideState[socdMappedKey] = 0;
}

/**
 * SOCD handling when a key is pressed
 * Release the opposing key if it's currently pressed
 */
static void handleSOCDOnPress(int keyIndex) {
  int socdMappedKey = getSOCDPairedKey(keyIndex);

  // No SOCD mapping for this key
  if (socdMappedKey == -1)
    return;

  // If the opposing key is not pressed, nothing to do
  int mappedKeyState = states[socdMappedKey];
  if (mappedKeyState == 0)
    return;

  // Mark the opposing key as overridden by SOCD
  socdOverrideState[socdMappedKey] = 1;

  // Release the opposing key
  if (settings_is_keyboard_enabled()) {
    if (settings_is_nkro_enabled()) {
      usb_hid_nkro_key_release(getKeyHIDCode(socdMappedKey));
    } else {
      usb_hid_key_release(getKeyHIDCode(socdMappedKey));
    }
  }
}

void press(int keyIndex, int rapid) {
  if (keyIndex < 0 || keyIndex >= 6)
    return;

  if (states[keyIndex] == 1) {
    return;
  }

  states[keyIndex] = 1;

  // Trigger reactive LED effect
  led_matrix_key_event(keyIndex, true);

  // Check settings for keyboard enabled
  if (!settings_is_keyboard_enabled()) {
    return;
  }
  
  // Check if keyboard output disabled for this key when gamepad active
  if (settings_is_gamepad_enabled()) {
    const settings_key_t *key_settings = settings_get_key(keyIndex);
    if (key_settings && key_settings->disable_kb_on_gamepad) {
      return;
    }
  }

  // Handle SOCD before sending key press
  handleSOCDOnPress(keyIndex);

  // Send key press
  if (settings_is_nkro_enabled()) {
    usb_hid_nkro_key_press(getKeyHIDCode(keyIndex));
  } else {
    usb_hid_key_press(getKeyHIDCode(keyIndex));
  }
}

void release(int keyIndex, int rapid) {
  if (keyIndex < 0 || keyIndex >= 6)
    return;

  if (states[keyIndex] == 0) {
    return;
  }

  states[keyIndex] = 0;

  // Trigger reactive LED effect
  led_matrix_key_event(keyIndex, false);

  // Check settings for keyboard enabled
  if (!settings_is_keyboard_enabled()) {
    return;
  }

  // Check if keyboard output disabled for this key when gamepad active
  if (settings_is_gamepad_enabled()) {
    const settings_key_t *key_settings = settings_get_key(keyIndex);
    if (key_settings && key_settings->disable_kb_on_gamepad) {
      return;
    }
  }

  // Handle SOCD after release (restore opposing key if needed)
  handleSOCDOnRelease(keyIndex);

  // Send key release
  if (settings_is_nkro_enabled()) {
    usb_hid_nkro_key_release(getKeyHIDCode(keyIndex));
  } else {
    usb_hid_key_release(getKeyHIDCode(keyIndex));
  }
}

void handleTrigger(int keyIndex, int currentVoltage) {
  if (keyIndex < 0 || keyIndex >= 6)
    return;

  int lastState = states[keyIndex];
  float lastDistance = distances[keyIndex];

  float correctedCurrentVoltage = getCorrectedValue(keyIndex, currentVoltage);
  float currentDistance = getValueFromLUT(correctedCurrentVoltage);

  // Si aucune variation de distance, on ne fait rien
  if (lastDistance == currentDistance) {
    return;
  }

  // TODO: Review normalization approach
  // Update gamepad axis with current key distance
  // Distance is 0.0 (released) to ~4.0mm, normalize to 0-1 range
  // Assuming max travel is around 4mm, clamp and normalize
  float normalizedDistance = currentDistance;
  if (normalizedDistance < 0.0f)
    normalizedDistance = 0.0f;
  if (normalizedDistance > 1.0f)
    normalizedDistance = 1.0f;
  usb_gamepad_set_axis_from_distance(keyIndex, normalizedDistance);

  // Get per-key settings
  float actuationPoint = getActuationPoint(keyIndex);
  float releasePoint = getReleasePoint(keyIndex);
  int rapidTriggerEnabled = isRapidTriggerEnabled(keyIndex);
  float rapidPressSensitivity = getRapidTriggerPressSensitivity(keyIndex);
  float rapidReleaseSensitivity = getRapidTriggerReleaseSensitivity(keyIndex);

  /**
   * NORMAL RELEASE DETECTION
   */

  // Si la touche est au dessus du point d'activation
  if (currentDistance < releasePoint) {
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
    // Et que le rapid trigger est activé
    if (lastState == 0 && lastDistance >= actuationPoint && rapidTriggerEnabled) {
      float minTopDistance = minTopDistances[keyIndex];

      // Si on est descendu de plus que le delta depuis le point le plus haut
      // atteint
      if (currentDistance - minTopDistance >= rapidPressSensitivity) {
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
    // Et que le rapid trigger est activé
    if (lastState == 1 && rapidTriggerEnabled) {
      float maxBottomDistance = maxBottomDistances[keyIndex];

      // Si on est remonté de plus que le delta depuis le point le plus bas
      // atteint
      if (maxBottomDistance - currentDistance >= rapidReleaseSensitivity) {
        // Rapid release
        updateKeyData(keyIndex, currentDistance, 1);
        release(keyIndex, 1);
        return;
      }
    }
  }

  updateKeyData(keyIndex, currentDistance, 0);
}