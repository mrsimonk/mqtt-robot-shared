#pragma once

#include "esp_err.h"

typedef struct {
  void (*on_wifi_connecting)(void);
  void (*on_wifi_connected)(void);
  void (*on_wifi_disconnected)(void);
} wifi_handlers_t;

// Initialize Wi-Fi in station mode, connect using configured SSID/password,
// and block until an IPv4 address is obtained (or a retry limit is hit).
//
// This is a minimal replacement for protocol_examples_common::example_connect().
void wifi_set_handlers(const wifi_handlers_t *handlers);

esp_err_t wifi_init_sta(void);
