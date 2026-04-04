#include "trigger.h"
#include "led_matrix.h"
#include "analog/lut.h"
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
#define DEFAULT_RAPID_TRIGGER_DELTA_UM 500  // 0.5mm = 500um default sensitivity

// Default actuation point (1.2mm)
#define DEFAULT_ACTUATION_POINT_UM 1200

// Default release point (1.2mm)
#define DEFAULT_RELEASE_POINT_UM 1200

// Valeur maximale d'appuie atteinte lorsque la touche est pressée puis en cours
// de relachement (en micromètres)
static int32_t maxBottomDistances_um[6] = {0, 0, 0, 0, 0, 0};
// Valeur minimale d'appuie atteinte lorsque la touche est relachée puis en
// cours d'appuie (en micromètres)
static int32_t minTopDistances_um[6] = {0, 0, 0, 0, 0, 0};

// Dernière valeur d'appuie lue (en micromètres)
static int32_t distances_um[6] = {0, 0, 0, 0, 0, 0};

// Distance normalisée (0..1) gardée pour compatibilité debug/HID
float distances[6] = {0, 0, 0, 0, 0, 0};

// Dernier état de la touche (0 = relachée, 1 = appuyée)
int states[6] = {0, 0, 0, 0, 0, 0};

// Whether the key is currently overridden by SOCD
static int socdOverrideState[6] = {0, 0, 0, 0, 0, 0};

// Flag pour éviter les faux triggers au premier échantillon
static uint8_t key_initialized[6] = {0, 0, 0, 0, 0, 0};

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
 * @brief Get actuation point in um for a key from settings
 * settings: actuation_point_mm est en 0.1mm => 0.1mm = 100um
 */
static int32_t getActuationPointUm(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return (int32_t)s->keys[keyIndex].actuation_point_mm * 100;
  }
  return (int32_t)DEFAULT_ACTUATION_POINT_UM;
}

/**
 * @brief Get release point in um for a key from settings
 * settings: release_point_mm est en 0.1mm => 0.1mm = 100um
 */
static int32_t getReleasePointUm(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return (int32_t)s->keys[keyIndex].release_point_mm * 100;
  }
  return (int32_t)DEFAULT_RELEASE_POINT_UM;
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
 * @brief Get rapid trigger press sensitivity in um
 * settings: rapid_trigger_press est en 0.01mm => 0.01mm = 10um
 */
static int32_t getRapidTriggerPressSensitivityUm(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return (int32_t)s->keys[keyIndex].rapid_trigger_press * 10;
  }
  return (int32_t)DEFAULT_RAPID_TRIGGER_DELTA_UM;
}

static int32_t getRapidTriggerReleaseSensitivityUm(int keyIndex) {
  const settings_t *s = settings_get();
  if (s) {
    return (int32_t)s->keys[keyIndex].rapid_trigger_release * 10;
  }
  return (int32_t)DEFAULT_RAPID_TRIGGER_DELTA_UM;
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

  for (int i = 0; i < 6; i++) {
    maxBottomDistances_um[i] = 0;
    minTopDistances_um[i] = 0;
    distances_um[i] = 0;
    distances[i] = 0.0f;
    states[i] = 0;
    key_initialized[i] = 0;
  }
}

int getKeyState(int keyIndex) {
  if (keyIndex < 0 || keyIndex >= 6)
    return 0;
  return states[keyIndex];
}

uint16_t triggerGetDistance01mm(int keyIndex) {
  if (keyIndex < 0 || keyIndex >= 6)
    return 0;

  int32_t um = distances_um[keyIndex];
  if (um < 0)
    um = 0;

  // Convert um to 0.01mm units (10um per step), rounded to nearest.
  return (uint16_t)((um + 5) / 10);
}

static inline void updateKeyDataUm(int keyIndex, int32_t currentDistanceUm,
                                   int resetExtremums) {
  if (resetExtremums) {
    // Réinitialisation des points extrêmes
    minTopDistances_um[keyIndex] = currentDistanceUm;
    maxBottomDistances_um[keyIndex] = currentDistanceUm;
  } else {
    // Mise à jour des points extrêmes
    // Si on remonte (distance diminue), on met à jour le minimum
    if (currentDistanceUm < minTopDistances_um[keyIndex]) {
      minTopDistances_um[keyIndex] = currentDistanceUm;
    }

    // Si on descend (distance augmente), on met à jour le maximum
    if (currentDistanceUm > maxBottomDistances_um[keyIndex]) {
      maxBottomDistances_um[keyIndex] = currentDistanceUm;
    }
  }

  // Mise à jour de la dernière distance (à la fin!)
  distances_um[keyIndex] = currentDistanceUm;
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

  const int lastState = states[keyIndex];
  const int32_t lastDistanceUm = distances_um[keyIndex];

  const int correctedCurrentVoltage = getCorrectedValue(keyIndex, currentVoltage);
  const float currentDistanceMm = getValueFromLUT(correctedCurrentVoltage);
  const int32_t currentDistanceUm = (int32_t)(currentDistanceMm * 1000.0f);

  // Premier échantillon: initialiser sans déclencher d'événements
  if (!key_initialized[keyIndex]) {
    key_initialized[keyIndex] = 1;
    updateKeyDataUm(keyIndex, currentDistanceUm, 1);
    // distances[] est normalisé 0..1 pour debug/HID
    float norm0 = currentDistanceMm / 4.0f;
    if (norm0 < 0.0f) norm0 = 0.0f;
    if (norm0 > 1.0f) norm0 = 1.0f;
    distances[keyIndex] = norm0;
    usb_gamepad_set_axis_from_distance(keyIndex, norm0);
    return;
  }

  // Si aucune variation de distance, on ne fait rien
  if (lastDistanceUm == currentDistanceUm) {
    return;
  }

  // Update gamepad axis (distance en mm => normaliser à 0..1 sur 4mm)
  float normalizedDistance = currentDistanceMm / 4.0f;
  if (normalizedDistance < 0.0f)
    normalizedDistance = 0.0f;
  if (normalizedDistance > 1.0f)
    normalizedDistance = 1.0f;
  distances[keyIndex] = normalizedDistance;
  usb_gamepad_set_axis_from_distance(keyIndex, normalizedDistance);

  // Get per-key settings (en µm)
  const int32_t actuationPointUm = getActuationPointUm(keyIndex);
  const int32_t releasePointUm = getReleasePointUm(keyIndex);
  const int rapidTriggerEnabled = isRapidTriggerEnabled(keyIndex);
  const int32_t rapidPressSensitivityUm = getRapidTriggerPressSensitivityUm(keyIndex);
  const int32_t rapidReleaseSensitivityUm = getRapidTriggerReleaseSensitivityUm(keyIndex);

  /**
   * NORMAL RELEASE DETECTION
   */

  // Si la touche est au dessus du point d'activation
  if (currentDistanceUm < releasePointUm) {
    // Normal release
    updateKeyDataUm(keyIndex, currentDistanceUm, 1);
    release(keyIndex, 0);
    return;
  }

  // Si la touche est en descente
  if (lastDistanceUm < currentDistanceUm) {
    /**
     * NORMAL PRESS DETECTION
     */

    // Si on passe sous le point d'activation
    if (currentDistanceUm >= actuationPointUm && lastDistanceUm < actuationPointUm) {
      // Normal press
      updateKeyDataUm(keyIndex, currentDistanceUm, 1);
      press(keyIndex, 0);
      return;
    }

    /**
     * RAPID PRESS DETECTION
     */

    // Si on est déjà sous le point d'activation
    // Et que la touche est relachée
    // Et que le rapid trigger est activé
    if (lastState == 0 && lastDistanceUm >= actuationPointUm && rapidTriggerEnabled) {
      const int32_t minTopDistanceUm = minTopDistances_um[keyIndex];

      // Si on est descendu de plus que le delta depuis le point le plus haut
      // atteint
      if (currentDistanceUm - minTopDistanceUm >= rapidPressSensitivityUm) {
        // Rapid press
        updateKeyDataUm(keyIndex, currentDistanceUm, 1);
        press(keyIndex, 1);
        return;
      }
    }
  }

  // Si la touche est en montée
  if (lastDistanceUm > currentDistanceUm) {
    /**
     * RAPID RELEASE DETECTION
     */

    // Si la touche est déjà pressée
    // Et que le rapid trigger est activé
    if (lastState == 1 && rapidTriggerEnabled) {
      const int32_t maxBottomDistanceUm = maxBottomDistances_um[keyIndex];

      // Si on est remonté de plus que le delta depuis le point le plus bas
      // atteint
      if (maxBottomDistanceUm - currentDistanceUm >= rapidReleaseSensitivityUm) {
        // Rapid release
        updateKeyDataUm(keyIndex, currentDistanceUm, 1);
        release(keyIndex, 1);
        return;
      }
    }
  }

  updateKeyDataUm(keyIndex, currentDistanceUm, 0);
}