#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  float wheel_track_mm;
  float wheel_radius_mm;
  float min_speed_mm_per_s;
  float max_speed_mm_per_s;
  float ticks_per_revolution;
  bool brake_on_stop;
  bool enable_speed_control;
  float speed_kp;
  float speed_ki;
  float motor_gain_left;
  float motor_gain_right;
} protocol_drive_config_t;

typedef struct {
  void (*drive)(const char *direction,
                int32_t speed_mm_per_s,
                uint32_t duration_ms,
                uint32_t distance_mm);
  void (*turn)(int32_t radius_mm,
               int32_t angle_deg,
               int32_t speed_mm_per_s,
               uint32_t duration_ms);
  void (*stop)(void);
  void (*clear_queue)(void);
  void (*set_led_hsv)(uint16_t h, uint8_t s, uint8_t v);
  void (*set_drive_config)(const protocol_drive_config_t *config);
  void (*immediate)(float left_frac,
                    float right_frac,
                    uint32_t timeout_ms,
                    uint32_t now_ms);
} protocol_handlers_t;

void protocol_set_handlers(const protocol_handlers_t *handlers);

void protocol_handle_command_json(const char *data, size_t len);
