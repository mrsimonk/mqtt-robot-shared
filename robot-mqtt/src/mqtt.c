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

static void log_error_if_nonzero(const char *message, int error_code) {
  if (error_code != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
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
  int msg_id;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
      mqtt_publish_debug("connected");
      if (s_handlers.on_connected != NULL) {
        s_handlers.on_connected();
      }
      // Subscribe to robot command topic
      msg_id = esp_mqtt_client_subscribe(client, CONFIG_COMMAND_TOPIC, 1);
      ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", CONFIG_COMMAND_TOPIC, msg_id);
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
      if (s_handlers.on_disconnected != NULL) {
        s_handlers.on_disconnected();
      }
      break;

    case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
      mqtt_publish_debug("subscribed");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_PUBLISHED:
      ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_DATA:
      ESP_LOGD(TAG, "MQTT_EVENT_DATA");
      // printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
      // printf("DATA=%.*s\r\n", event->data_len, event->data);
      // For now, treat all incoming data on CONFIG_COMMAND_TOPIC as
      // JSON commands and forward to the registered handler.
      if (s_handlers.on_command_json != NULL) {
        s_handlers.on_command_json(event->data, (size_t)event->data_len);
      }
      break;
    case MQTT_EVENT_ERROR:
      ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        log_error_if_nonzero("reported from esp-tls",
                            event->error_handle->esp_tls_last_esp_err);
        log_error_if_nonzero("reported from tls stack",
                            event->error_handle->esp_tls_stack_err);
        log_error_if_nonzero("captured as transport's socket errno",
                            event->error_handle->esp_transport_sock_errno);
        ESP_LOGI(TAG, "Last errno string (%s)",
                strerror(event->error_handle->esp_transport_sock_errno));
      }
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
