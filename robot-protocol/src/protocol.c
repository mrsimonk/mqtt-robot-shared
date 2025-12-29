#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include <cJSON.h>

#include "../include/protocol.h"

static const char *TAG = "protocol";

static protocol_handlers_t s_handlers;

void protocol_set_handlers(const protocol_handlers_t *handlers)
{
  if (handlers != NULL) {
    s_handlers = *handlers;
  } else {
    protocol_handlers_t empty = {0};
    s_handlers = empty;
  }
}

static bool handle_drive_command(const cJSON *command) {
  const cJSON *direction =
      cJSON_GetObjectItemCaseSensitive(command, "direction");
  const cJSON *speed = cJSON_GetObjectItemCaseSensitive(command, "speed");
  const cJSON *duration = cJSON_GetObjectItemCaseSensitive(command, "duration");
  const cJSON *distance = cJSON_GetObjectItemCaseSensitive(command, "distance");

  if (!cJSON_IsString(direction) || direction->valuestring == NULL ||
      !cJSON_IsNumber(speed)) {
    ESP_LOGW(TAG, "Invalid drive command payload");
    return false;
  }

  int32_t speed_mm_per_s = (int32_t)speed->valuedouble;
  uint32_t duration_ms = (duration != NULL && cJSON_IsNumber(duration))
                             ? (uint32_t)duration->valuedouble
                             : 0u;
  uint32_t distance_mm = (distance != NULL && cJSON_IsNumber(distance))
                             ? (uint32_t)distance->valuedouble
                             : 0u;
  ESP_LOGD(TAG,
           "drive: direction=%s, speed=%d, duration=%d, distance=%d",
           direction->valuestring, speed_mm_per_s, duration_ms, distance_mm);

  if (s_handlers.drive != NULL) {
    s_handlers.drive(direction->valuestring,
                     speed_mm_per_s,
                     duration_ms,
                     distance_mm);
  }
  return true;
}

static bool handle_turn_command(const cJSON *command) {
  const cJSON *radius = cJSON_GetObjectItemCaseSensitive(command, "radius");
  const cJSON *angle = cJSON_GetObjectItemCaseSensitive(command, "angle");
  const cJSON *speed = cJSON_GetObjectItemCaseSensitive(command, "speed");
  const cJSON *duration = cJSON_GetObjectItemCaseSensitive(command, "duration");

  if (!cJSON_IsNumber(radius) || !cJSON_IsNumber(angle)) {
    ESP_LOGW(TAG, "Invalid turn command payload (radius/angle)");
    return false;
  }

  int32_t radius_mm = (int32_t)radius->valuedouble;
  int32_t angle_deg = (int32_t)angle->valuedouble;

  int32_t speed_mm_per_s = 0;
  uint32_t duration_ms = 0u;

  if (cJSON_IsNumber(speed)) {
    speed_mm_per_s = (int32_t)speed->valuedouble;
  }
  if (cJSON_IsNumber(duration)) {
    duration_ms = (uint32_t)duration->valuedouble;
  }

  // Require at least one of speed or duration.
  if (speed_mm_per_s <= 0 && duration_ms == 0u) {
    ESP_LOGW(TAG, "Turn command requires speed or duration");
    return false;
  }

  ESP_LOGD(TAG,
           "turn: radius=%d, angle=%d, speed=%d, duration=%u",
           radius_mm, angle_deg, speed_mm_per_s, (unsigned)duration_ms);

  if (s_handlers.turn != NULL) {
    s_handlers.turn(radius_mm, angle_deg, speed_mm_per_s, duration_ms);
  }
  return true;
}

static bool handle_led_hsv_command(const cJSON *command) {
  const cJSON *h = cJSON_GetObjectItemCaseSensitive(command, "h");
  const cJSON *s = cJSON_GetObjectItemCaseSensitive(command, "s");
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(command, "v");

  if (!cJSON_IsNumber(h)) {
    ESP_LOGW(TAG, "Invalid led_hsv command payload (missing h)");
    return false;
  }

  uint16_t hue = (uint16_t)h->valuedouble;
  uint8_t sat = cJSON_IsNumber(s) ? (uint8_t)s->valuedouble : 255u;
  uint8_t val = cJSON_IsNumber(v) ? (uint8_t)v->valuedouble : 32u;

  ESP_LOGD(TAG, "led_hsv: h=%u s=%u v=%u", (unsigned)hue,
           (unsigned)sat, (unsigned)val);

  if (s_handlers.set_led_hsv != NULL) {
    s_handlers.set_led_hsv(hue, sat, val);
  }
  return true;
}

static bool handle_immediate_command(const cJSON *command) {
  const cJSON *left = cJSON_GetObjectItemCaseSensitive(command, "left");
  const cJSON *right = cJSON_GetObjectItemCaseSensitive(command, "right");
  const cJSON *timeout =
      cJSON_GetObjectItemCaseSensitive(command, "timeout_ms");

  if (!cJSON_IsNumber(left) || !cJSON_IsNumber(right)) {
    ESP_LOGW(TAG, "Invalid immediate command payload (left/right)");
    return false;
  }

  float left_frac = (float)left->valuedouble;
  float right_frac = (float)right->valuedouble;

  uint32_t timeout_ms =
      (timeout != NULL && cJSON_IsNumber(timeout))
          ? (uint32_t)timeout->valuedouble
          : 200u;

  uint32_t now_ms = (uint32_t)esp_log_timestamp();

  ESP_LOGD(TAG, "immediate: left=%f, right=%f, timeout=%u, now=%u", left_frac,
           right_frac, (unsigned)timeout_ms, (unsigned)now_ms);

  if (s_handlers.immediate != NULL) {
    s_handlers.immediate(left_frac, right_frac, timeout_ms, now_ms);
  }
  return true;
}

static bool handle_single_command_object(const cJSON *command) {
  const cJSON *kind = cJSON_GetObjectItemCaseSensitive(command, "kind");
  if (!cJSON_IsString(kind) || kind->valuestring == NULL) {
    ESP_LOGW(TAG, "JSON command missing kind");
    return false;
  }

  ESP_LOGD(TAG, "parsed command - kind=%s", kind->valuestring);

  if (strcmp(kind->valuestring, "drive") == 0) {
    return handle_drive_command(command);
  }
  if (strcmp(kind->valuestring, "turn") == 0) {
    return handle_turn_command(command);
  }
  if (strcmp(kind->valuestring, "led_hsv") == 0) {
    return handle_led_hsv_command(command);
  }
  if (strcmp(kind->valuestring, "immediate") == 0) {
    return handle_immediate_command(command);
  }
  if (strcmp(kind->valuestring, "stop") == 0) {
    if (s_handlers.stop != NULL) {
      s_handlers.stop();
    }
    return true;
  }
  if (strcmp(kind->valuestring, "wait") == 0) {
    // will delay for a specified amount of time
    // drive_command_wait();
    return true;
  }
  if (strcmp(kind->valuestring, "pause") == 0) {
    // will stop the current command, stop moving, but keep the queue
    // drive_command_pause();
    return true;
  }
  if (strcmp(kind->valuestring, "resume") == 0) {
    // if paused, will resume the current command, and continue processing the
    // queue drive_command_resume();
    return true;
  }
  if (strcmp(kind->valuestring, "clear_queue") == 0) {
    // clears the queue, and stops the current command (?)
    if (s_handlers.clear_queue != NULL) {
      s_handlers.clear_queue();
    }
    return true;
  }

  ESP_LOGW(TAG, "Unknown command kind: %s", kind->valuestring);
  return false;
}

static void handle_sequence_type(const cJSON *root) {
  const cJSON *steps = cJSON_GetObjectItemCaseSensitive(root, "steps");
  if (!cJSON_IsArray(steps)) {
    ESP_LOGW(TAG, "Sequence missing steps array");
    return;
  }

  const cJSON *step = NULL;
  cJSON_ArrayForEach(step, steps) {
    if (!cJSON_IsObject(step)) {
      ESP_LOGW(TAG, "Sequence step is not an object");
      continue;
    }

    (void)handle_single_command_object(step);
  }
}
static void handle_config_type(const cJSON *root) {
  const cJSON *drive = cJSON_GetObjectItemCaseSensitive(root, "drive");
  if (!cJSON_IsObject(drive)) {
    return;
  }

  protocol_drive_config_t cfg = {0};

  const cJSON *track =
      cJSON_GetObjectItemCaseSensitive(drive, "wheel_track_mm");
  const cJSON *radius =
      cJSON_GetObjectItemCaseSensitive(drive, "wheel_radius_mm");
  const cJSON *min_speed =
      cJSON_GetObjectItemCaseSensitive(drive, "min_speed_mm_per_s");
  const cJSON *max_speed =
      cJSON_GetObjectItemCaseSensitive(drive, "max_speed_mm_per_s");
  const cJSON *ticks_rev =
      cJSON_GetObjectItemCaseSensitive(drive, "ticks_per_revolution");
  const cJSON *brake_on_stop =
      cJSON_GetObjectItemCaseSensitive(drive, "brake_on_stop");
  const cJSON *enable_speed_control =
      cJSON_GetObjectItemCaseSensitive(drive, "enable_speed_control");
  const cJSON *speed_kp =
      cJSON_GetObjectItemCaseSensitive(drive, "speed_kp");
  const cJSON *speed_ki =
      cJSON_GetObjectItemCaseSensitive(drive, "speed_ki");
  const cJSON *motor_gain_left =
      cJSON_GetObjectItemCaseSensitive(drive, "motor_gain_left");
  const cJSON *motor_gain_right =
      cJSON_GetObjectItemCaseSensitive(drive, "motor_gain_right");

  if (cJSON_IsNumber(track)) {
    cfg.wheel_track_mm = (float)track->valuedouble;
  }
  if (cJSON_IsNumber(radius)) {
    cfg.wheel_radius_mm = (float)radius->valuedouble;
  }
  if (cJSON_IsNumber(min_speed)) {
    cfg.min_speed_mm_per_s = (float)min_speed->valuedouble;
  }
  if (cJSON_IsNumber(max_speed)) {
    cfg.max_speed_mm_per_s = (float)max_speed->valuedouble;
  }

  if (cJSON_IsNumber(ticks_rev)) {
    cfg.ticks_per_revolution = (float)ticks_rev->valuedouble;
  }

  if (cJSON_IsBool(brake_on_stop)) {
    cfg.brake_on_stop = cJSON_IsTrue(brake_on_stop);
  }

  if (cJSON_IsBool(enable_speed_control)) {
    cfg.enable_speed_control = cJSON_IsTrue(enable_speed_control);
  }

  if (cJSON_IsNumber(speed_kp)) {
    cfg.speed_kp = (float)speed_kp->valuedouble;
  }
  if (cJSON_IsNumber(speed_ki)) {
    cfg.speed_ki = (float)speed_ki->valuedouble;
  }

  if (cJSON_IsNumber(motor_gain_left)) {
    cfg.motor_gain_left = (float)motor_gain_left->valuedouble;
  }
  if (cJSON_IsNumber(motor_gain_right)) {
    cfg.motor_gain_right = (float)motor_gain_right->valuedouble;
  }

  if (s_handlers.set_drive_config != NULL) {
    s_handlers.set_drive_config(&cfg);
  }
}

static void handle_command_type(const cJSON *root) {
  const cJSON *command = cJSON_GetObjectItemCaseSensitive(root, "command");
  if (!cJSON_IsObject(command)) {
    ESP_LOGW(TAG, "JSON command missing command object");
    return;
  }
  (void)handle_single_command_object(command);
}

static void handle_command(const cJSON *root, const cJSON *type) {
  if (strcmp(type->valuestring, "command") == 0) {
    handle_command_type(root);
  } else if (strcmp(type->valuestring, "sequence") == 0) {
    handle_sequence_type(root);
  } else if (strcmp(type->valuestring, "config") == 0) {
    handle_config_type(root);
  } else {
    ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
  }

  cJSON_Delete((cJSON *)root);
}

void protocol_handle_command_json(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }

  char *buffer = malloc(len + 1u);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer for JSON parse");
    return;
  }

  memcpy(buffer, data, len);
  buffer[len] = '\0';

  cJSON *root = cJSON_Parse(buffer);
  free(buffer);

  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to parse JSON command");
    return;
  }

  const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
  if (!cJSON_IsString(type) || type->valuestring == NULL) {
    ESP_LOGW(TAG, "JSON command missing type");
    cJSON_Delete(root);
    return;
  }

  ESP_LOGD(TAG, "parsed json - type=%s", type->valuestring);
  handle_command(root, type);
}
