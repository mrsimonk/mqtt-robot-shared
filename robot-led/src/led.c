#include "esp_log.h"
#include "led_strip.h"
#include "led.h"

static const char *TAG = "led";
#define LED_GPIO 8

#define LED_HUE_WIFI_CONNECTING   60u   // yellow/orange-ish
#define LED_HUE_READY             120u  // green-ish
#define LED_HUE_MQTT_CONNECTING   220u  // blue-ish
#define LED_HUE_COMMAND_ACTIVE    280u  // purple-ish
#define LED_HUE_ERROR             0u    // red-ish

static led_strip_handle_t led_strip;

void led_init(void) {
  led_strip_config_t strip_config = {.strip_gpio_num = LED_GPIO,
                                     .max_leds = 1,
                                     .color_component_format =
                                         LED_STRIP_COLOR_COMPONENT_FMT_RGB};
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000, // 10MHz
      .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);
}

void set_led_color(uint16_t color) {
  led_strip_set_pixel_hsv(led_strip, 0, color, 255, 32);
  led_strip_refresh(led_strip);
}

void led_set_hsv(uint16_t h, uint8_t s, uint8_t v) {
  led_strip_set_pixel_hsv(led_strip, 0, h, s, v);
  led_strip_refresh(led_strip);
}

void led_set_status(led_status_t status) {
  ESP_LOGD(TAG, "Setting LED status: %d", status);
  switch (status) {
    case LED_STATUS_OFF:
      led_strip_clear(led_strip);
      led_strip_refresh(led_strip);
      break;
    case LED_STATUS_WIFI_CONNECTING:
      set_led_color(LED_HUE_WIFI_CONNECTING);
      break;
    case LED_STATUS_WIFI_CONNECTED:
    case LED_STATUS_MQTT_CONNECTED:
    case LED_STATUS_READY:  
      set_led_color(LED_HUE_READY);
      break;
    case LED_STATUS_MQTT_CONNECTING:
      set_led_color(LED_HUE_MQTT_CONNECTING);
      break;
    case LED_STATUS_COMMAND_ACTIVE:
      set_led_color(LED_HUE_COMMAND_ACTIVE);
      break;
    case LED_STATUS_ERROR:
    default:
      set_led_color(LED_HUE_ERROR);
      break;
  }
}