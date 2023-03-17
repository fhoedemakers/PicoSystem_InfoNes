#include "rgbled.hpp"

namespace pimoroni {
  constexpr uint8_t GAMMA_8BIT[256] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
      2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5,
      6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11,
      11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18,
      19, 19, 20, 21, 21, 22, 22, 23, 23, 24, 25, 25, 26, 27, 27, 28,
      29, 29, 30, 31, 31, 32, 33, 34, 34, 35, 36, 37, 37, 38, 39, 40,
      40, 41, 42, 43, 44, 45, 46, 46, 47, 48, 49, 50, 51, 52, 53, 54,
      55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
      71, 72, 73, 74, 76, 77, 78, 79, 80, 81, 83, 84, 85, 86, 88, 89,
      90, 91, 93, 94, 95, 96, 98, 99, 100, 102, 103, 104, 106, 107, 109, 110,
      111, 113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 128, 129, 131, 132, 134,
      135, 137, 138, 140, 142, 143, 145, 146, 148, 150, 151, 153, 155, 157, 158, 160,
      162, 163, 165, 167, 169, 170, 172, 174, 176, 178, 179, 181, 183, 185, 187, 189,
      191, 193, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 218, 220,
      222, 224, 227, 229, 231, 233, 235, 237, 239, 241, 244, 246, 248, 250, 252, 255};
  void RGBLED::set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    led_r = r;
    led_g = g;
    led_b = b;
    update_pwm();
  }

  void RGBLED::set_brightness(uint8_t brightness) {
    led_brightness = brightness;
    update_pwm();
  }

  void RGBLED::set_hsv(float h, float s, float v) {
    float i = floor(h * 6.0f);
    float f = h * 6.0f - i;
    v *= 255.0f;
    uint8_t p = v * (1.0f - s);
    uint8_t q = v * (1.0f - f * s);
    uint8_t t = v * (1.0f - (1.0f - f) * s);

    switch (int(i) % 6) {
      case 0: led_r = v; led_g = t; led_b = p; break;
      case 1: led_r = q; led_g = v; led_b = p; break;
      case 2: led_r = p; led_g = v; led_b = t; break;
      case 3: led_r = p; led_g = q; led_b = v; break;
      case 4: led_r = t; led_g = p; led_b = v; break;
      case 5: led_r = v; led_g = p; led_b = q; break;
    }

    update_pwm();
  }

  void RGBLED::update_pwm() {
    uint16_t r16 = GAMMA_8BIT[led_r];
    uint16_t g16 = GAMMA_8BIT[led_g];
    uint16_t b16 = GAMMA_8BIT[led_b];
    r16 *= led_brightness;
    g16 *= led_brightness;
    b16 *= led_brightness;
    if(polarity == Polarity::ACTIVE_LOW) {
      r16 = UINT16_MAX - r16;
      g16 = UINT16_MAX - g16;
      b16 = UINT16_MAX - b16;
    }
    pwm_set_gpio_level(pin_r, r16);
    pwm_set_gpio_level(pin_g, g16);
    pwm_set_gpio_level(pin_b, b16);
  }
};