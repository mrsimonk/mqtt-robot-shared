#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "../include/mqtt.h"

static const char *TAG = "mqtt_client";
static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_handlers_t s_handlers;

static char *s_rx_buffer = NULL;
static size_t s_rx_buffer_len = 0u;
static size_t s_rx_expected_len = 0u;

static void log_error_if_nonzero(const char *message, int error_code) {
  if (error_code != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
  }
}

static void mqtt_handle_connected(esp_mqtt_client_handle_t client)
{
  int msg_id;

  ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
  mqtt_publish_debug("connected");
  if (s_handlers.on_connected != NULL) {
    s_handlers.on_connected();
  }

  msg_id = esp_mqtt_client_subscribe(client, CONFIG_COMMAND_TOPIC, 1);
  ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", CONFIG_COMMAND_TOPIC, msg_id);
}

static void mqtt_handle_disconnected(void)
{
  ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
  if (s_handlers.on_disconnected != NULL) {
    s_handlers.on_disconnected();
  }
}

static void mqtt_handle_subscribed(const esp_mqtt_event_handle_t event)
{
  ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
  mqtt_publish_debug("subscribed");
}

static void mqtt_handle_unsubscribed(const esp_mqtt_event_handle_t event)
{
  ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
}

static void mqtt_handle_published(const esp_mqtt_event_handle_t event)
{
  ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
}

static void mqtt_handle_data(const esp_mqtt_event_handle_t event)
{
  ESP_LOGD(TAG, "MQTT_EVENT_DATA len=%d total=%d off=%d", event->data_len,
           event->total_data_len, event->current_data_offset);

  if (s_handlers.on_command_json == NULL) {
    return;
  }

  if (event->total_data_len <= 0 || event->data_len <= 0) {
    return;
  }

  if (event->current_data_offset == 0) {
    if (s_rx_expected_len != 0u || s_rx_buffer != NULL) {
      free(s_rx_buffer);
      s_rx_buffer = NULL;
      s_rx_buffer_len = 0u;
      s_rx_expected_len = 0u;
    }

    size_t total = (size_t)event->total_data_len;
    const size_t kMaxJsonLen = 8192u;
    if (total == 0u || total > kMaxJsonLen) {
      ESP_LOGW(TAG, "MQTT payload too large or zero (len=%u)",
               (unsigned)total);
      return;
    }

    s_rx_buffer = malloc(total);
    if (s_rx_buffer == NULL) {
      ESP_LOGE(TAG, "Failed to allocate MQTT RX buffer (%u bytes)",
               (unsigned)total);
      return;
    }
    s_rx_buffer_len = 0u;
    s_rx_expected_len = total;
  }

  if (s_rx_buffer == NULL || s_rx_expected_len == 0u) {
    return;
  }

  if ((size_t)event->current_data_offset != s_rx_buffer_len) {
    ESP_LOGW(TAG,
             "MQTT data offset mismatch (off=%d, buf_len=%u)",
             event->current_data_offset, (unsigned)s_rx_buffer_len);
    free(s_rx_buffer);
    s_rx_buffer = NULL;
    s_rx_buffer_len = 0u;
    s_rx_expected_len = 0u;
    return;
  }

  if (s_rx_buffer_len + (size_t)event->data_len > s_rx_expected_len) {
    ESP_LOGW(TAG, "MQTT data overflow (buf_len=%u, chunk=%d, expect=%u)",
             (unsigned)s_rx_buffer_len, event->data_len,
             (unsigned)s_rx_expected_len);
    free(s_rx_buffer);
    s_rx_buffer = NULL;
    s_rx_buffer_len = 0u;
    s_rx_expected_len = 0u;
    return;
  }

  memcpy(s_rx_buffer + s_rx_buffer_len, event->data,
         (size_t)event->data_len);
  s_rx_buffer_len += (size_t)event->data_len;

  if (s_rx_buffer_len == s_rx_expected_len) {
    s_handlers.on_command_json(s_rx_buffer, s_rx_buffer_len);
    free(s_rx_buffer);
    s_rx_buffer = NULL;
    s_rx_buffer_len = 0u;
    s_rx_expected_len = 0u;
  }
}

static void mqtt_handle_error(const esp_mqtt_event_handle_t event)
{
  ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
  if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
    log_error_if_nonzero("reported from esp-tls",
                        event->error_handle->esp_tls_last_esp_err);
    log_error_if_nonzero("reported from tls stack",
                        event->error_handle->esp_tls_stack_err);
    log_error_if_nonzero("captured as transport's socket errno",
                        event->error_handle->esp_transport_sock_errno);
    ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x",
            event->error_handle->esp_tls_last_esp_err);
    ESP_LOGE(TAG, "Last error code reported from tls stack: 0x%x",
            event->error_handle->esp_tls_stack_err);
    ESP_LOGE(TAG, "socket errno: %d (%s)",
            event->error_handle->esp_transport_sock_errno,
            strerror(event->error_handle->esp_transport_sock_errno));
  }
}

void mqtt_set_handlers(const mqtt_handlers_t *handlers)
{
  if (handlers != NULL) {
    s_handlers = *handlers;
  } else {
    mqtt_handlers_t empty = {0};
    s_handlers = empty;
  }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  ESP_LOGD(TAG,
           "Event dispatched from event loop base=%s, event_id=%" PRIi32 "",
           base, event_id);
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      mqtt_handle_connected(client);
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqtt_handle_disconnected();
      break;

    case MQTT_EVENT_SUBSCRIBED:
      mqtt_handle_subscribed(event);
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      mqtt_handle_unsubscribed(event);
      break;
    case MQTT_EVENT_PUBLISHED:
      mqtt_handle_published(event);
      break;
    case MQTT_EVENT_DATA:
      mqtt_handle_data(event);
      break;
    case MQTT_EVENT_ERROR:
      mqtt_handle_error(event);
      break;
    default:
      ESP_LOGI(TAG, "Other event id:%d", event->event_id);
      break;
  }
}

void mqtt_init(void) {
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = CONFIG_BROKER_URL,
      .credentials.username = CONFIG_BROKER_USERNAME,
      .credentials.authentication.password = CONFIG_BROKER_PASSWORD,
      .session.keepalive = 10
  };

  s_client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(s_client,
                                 ESP_EVENT_ANY_ID,
                                 mqtt_event_handler,
                                 NULL);
  esp_mqtt_client_start(s_client);
}

void mqtt_publish_debug(const char *payload)
{
  if (s_client == NULL || payload == NULL) {
    return;
  }

  // QoS0, non-retained debug message on robot/debug
  (void)esp_mqtt_client_publish(s_client,
                                "robot/debug",
                                payload,
                                0,
                                0,
                                0);
}

void mqtt_publish_command(const char *payload)
{
  if (s_client == NULL || payload == NULL) {
    return;
  }

  // Publish command JSON to the configured command topic
  (void)esp_mqtt_client_publish(s_client,
                                CONFIG_COMMAND_TOPIC,
                                payload,
                                0,
                                1,
                                0);
}
