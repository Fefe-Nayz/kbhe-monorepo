#include "led_overlay_math.h"
#include "settings.h"

#include <assert.h>
#include <stdio.h>

static void test_filled_only_unfilled_pixel_is_transparent(void) {
  uint8_t r = 240u;
  uint8_t g = 80u;
  uint8_t b = 20u;
  uint8_t alpha = 220u;
  uint8_t base = 137u;

  assert(!led_overlay_prepare_progress_pixel(true, 0u, &r, &g, &b,
                                              &alpha));
  assert(r == 240u && g == 80u && b == 20u && alpha == 220u);
  /* A compositor which honors the false return leaves the effect untouched. */
  assert(base == 137u);
}

static void test_filled_only_partial_pixel_crossfades_by_alpha(void) {
  uint8_t r = 255u;
  uint8_t g = 64u;
  uint8_t b = 0u;
  uint8_t alpha = 200u;

  assert(led_overlay_prepare_progress_pixel(true, 128u, &r, &g, &b,
                                             &alpha));
  assert(r == 255u && g == 64u && b == 0u);
  assert(alpha == led_overlay_scale_u8(200u, 128u));
  assert(led_overlay_blend_channel(100u, 255u, 0u) == 100u);
}

static void test_classic_unfilled_pixel_retains_shaded_track(void) {
  uint8_t r = 255u;
  uint8_t g = 64u;
  uint8_t b = 32u;
  uint8_t alpha = 200u;

  assert(led_overlay_prepare_progress_pixel(false, 0u, &r, &g, &b,
                                             &alpha));
  assert(r == 0u && g == 0u && b == 0u && alpha == 200u);
  assert(led_overlay_blend_channel(100u, r, alpha) < 100u);
}

static void test_persisted_rotary_flag_survives_motion_updates(void) {
  settings_rotary_encoder_t rotary = {0};

  settings_rotary_set_progress_filled_only(&rotary, true);
  settings_rotary_set_motion(&rotary, 16u, 3u);
  assert(settings_rotary_is_progress_filled_only(&rotary));
  assert(settings_rotary_get_sensitivity(&rotary) == 16u);
  assert(settings_rotary_get_acceleration(&rotary) == 3u);

  settings_rotary_set_progress_filled_only(&rotary, false);
  assert(!settings_rotary_is_progress_filled_only(&rotary));
  assert(settings_rotary_get_sensitivity(&rotary) == 16u);
  assert(settings_rotary_get_acceleration(&rotary) == 3u);
}

int main(void) {
  test_filled_only_unfilled_pixel_is_transparent();
  test_filled_only_partial_pixel_crossfades_by_alpha();
  test_classic_unfilled_pixel_retains_shaded_track();
  test_persisted_rotary_flag_survives_motion_updates();
  puts("led_overlay_math_test: ok");
  return 0;
}
