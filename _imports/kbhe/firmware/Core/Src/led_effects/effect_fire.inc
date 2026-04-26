static void effect_fire(void) {
  uint8_t heat_boost = (uint8_t)(((uint16_t)effect_param(0, 160u) * 64u) / 255u);
  uint8_t ember_floor = (uint8_t)(((uint16_t)effect_param(1, 96u) * 80u) / 255u);
  uint8_t cooling = (uint8_t)(16u + ((uint16_t)effect_param(2, 96u) * 96u) / 255u);
  /* Palette: 0=classic fire, 1=amber, 2=ice. */
  uint8_t palette = effect_param(3, 0u) % 3u;
  for (uint8_t i = 0; i < LED_MATRIX_NUM_LEDS; i++) {
    uint8_t x_term = scale_coord_to_u8(led_pos_x[i], LED_LAYOUT_MAX_X);
    uint8_t y_term = 255u - scale_coord_to_u8(led_pos_y[i], LED_LAYOUT_MAX_Y);
    uint8_t heat = (uint8_t)((y_term * 3u) / 4u + ((effect_offset + x_term) % 64u) +
                             heat_boost);
    uint8_t r, g, b;
    heat = (heat > cooling) ? (uint8_t)(heat - cooling) : 0u;

    if (palette == 1u) {
      if (heat > 220u) {
        r = 255u;
        g = 220u;
        b = 96u;
      } else if (heat > 170u) {
        r = 255u;
        g = 160u;
        b = 0u;
      } else if (heat > 100u) {
        r = 240u;
        g = 80u;
        b = 0u;
      } else if (heat > ember_floor) {
        r = (uint8_t)(112u + ember_floor / 2u);
        g = (uint8_t)(24u + ember_floor / 2u);
        b = 0u;
      } else {
        r = g = b = 0u;
      }
    } else if (palette == 2u) {
      if (heat > 220u) {
        r = 180u;
        g = 220u;
        b = 255u;
      } else if (heat > 170u) {
        r = 32u;
        g = 160u;
        b = 255u;
      } else if (heat > 100u) {
        r = 0u;
        g = 72u;
        b = 255u;
      } else if (heat > ember_floor) {
        r = 0u;
        g = (uint8_t)(32u + ember_floor / 2u);
        b = (uint8_t)(96u + ember_floor);
      } else {
        r = g = b = 0u;
      }
    } else {
      if (heat > 220u) {
        r = 255u;
        g = 255u;
        b = 160u;
      } else if (heat > 170u) {
        r = 255u;
        g = 170u;
        b = 0u;
      } else if (heat > 100u) {
        r = 255u;
        g = 72u;
        b = 0u;
      } else if (heat > ember_floor) {
        r = (uint8_t)(96u + ember_floor);
        g = 0u;
        b = 0u;
      } else {
        r = g = b = 0u;
      }
    }

    led_matrix_set_pixel_idx(i, r, g, b);
  }
}


