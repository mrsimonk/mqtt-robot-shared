#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "../include/wifi.h"

static const char *TAG = "wifi";

static SemaphoreHandle_t s_ip_sem = NULL;
static int s_retry_num = 0;

static wifi_handlers_t s_handlers;

#define WIFI_CONN_MAX_RETRY 5

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  s_retry_num++;
  if (s_retry_num > WIFI_CONN_MAX_RETRY) {
    ESP_LOGW(TAG, "Wi-Fi connect failed %d times, giving up", s_retry_num);
    if (s_handlers.on_wifi_disconnected != NULL) {
      s_handlers.on_wifi_disconnected();
    }
    if (s_ip_sem) {
      xSemaphoreGive(s_ip_sem);
    }
    return;
  }
  ESP_LOGI(TAG, "Wi-Fi disconnected, retrying (%d/%d)...", s_retry_num,
           WIFI_CONN_MAX_RETRY);
  if (s_handlers.on_wifi_connecting != NULL) {
    s_handlers.on_wifi_connecting();
  }
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
    ESP_LOGE(TAG, "esp_wifi_connect failed: 0x%x", err);
  }
}

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  ESP_LOGI(TAG, "Got IPv4 address: " IPSTR, IP2STR(&event->ip_info.ip));
  s_retry_num = 0;
  if (s_ip_sem) {
    xSemaphoreGive(s_ip_sem);
  }
  if (s_handlers.on_wifi_connected != NULL) {
    s_handlers.on_wifi_connected();
  }
}

esp_err_t wifi_init_sta(void) {
  esp_err_t err;

  ESP_ERROR_CHECK(esp_netif_init());

  // Network stack and default event loop must already be initialized
  // (esp_netif_init, esp_event_loop_create_default).

  esp_netif_t *netif = esp_netif_create_default_wifi_sta();
  if (!netif) {
    ESP_LOGE(TAG, "Failed to create default Wi-Fi STA netif");
    return ESP_FAIL;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                             &on_wifi_disconnect, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &on_got_ip, NULL));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  wifi_config_t wifi_config = {
      .sta = {
          .ssid = CONFIG_WIFI_SSID,
          .password = CONFIG_WIFI_PASSWORD,
      },
  };

  ESP_LOGI(TAG, "Connecting to SSID '%s'...", (char *)wifi_config.sta.ssid);

  if (s_handlers.on_wifi_connecting != NULL) {
    s_handlers.on_wifi_connecting();
  }

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_connect failed: 0x%x", err);
    return err;
  }

  s_ip_sem = xSemaphoreCreateBinary();
  if (!s_ip_sem) {
    ESP_LOGE(TAG, "Failed to create IP semaphore");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Waiting for IPv4 address...");
  xSemaphoreTake(s_ip_sem, portMAX_DELAY);
  vSemaphoreDelete(s_ip_sem);
  s_ip_sem = NULL;

  if (s_retry_num > WIFI_CONN_MAX_RETRY) {
    ESP_LOGE(TAG, "Failed to obtain IP after retries");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Wi-Fi connected and IP acquired");
  return ESP_OK;
}

void wifi_set_handlers(const wifi_handlers_t *handlers)
{
  if (handlers != NULL) {
    s_handlers = *handlers;
  } else {
    wifi_handlers_t empty = {0};
    s_handlers = empty;
  }
}
