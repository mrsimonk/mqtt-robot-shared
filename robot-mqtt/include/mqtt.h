#pragma once

#include <stddef.h>

typedef struct {
  // Called when a command message arrives on CONFIG_COMMAND_TOPIC.
  void (*on_command_json)(const char *data, size_t len);

  // Optional connection status notifications.
  void (*on_connected)(void);
  void (*on_disconnected)(void);
} mqtt_handlers_t;

void mqtt_set_handlers(const mqtt_handlers_t *handlers);

void mqtt_init(void);

// Publish a debug JSON payload to the robot/debug topic.
// The payload string must be a null-terminated JSON document.
void mqtt_publish_debug(const char *payload);

// Publish a command JSON payload to the configured command topic
// (CONFIG_COMMAND_TOPIC). The payload string must be a null-terminated
// JSON document.
void mqtt_publish_command(const char *payload);