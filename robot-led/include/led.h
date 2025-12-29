#include <stdint.h>

typedef enum {
  LED_STATUS_OFF = 0,
  LED_STATUS_WIFI_CONNECTING = 1,
  LED_STATUS_WIFI_CONNECTED = 2,
  LED_STATUS_MQTT_CONNECTING = 3,
  LED_STATUS_MQTT_CONNECTED = 4,
  LED_STATUS_READY = 5,
  LED_STATUS_COMMAND_ACTIVE = 6,
  LED_STATUS_ERROR = 7,
} led_status_t;

void led_init(void);
void set_led_color(uint16_t color);
void led_set_hsv(uint16_t h, uint8_t s, uint8_t v);
void led_set_status(led_status_t status);