/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <si7021.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif
/* The examples use WiFi configuration that you can set via project
   configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define WIFI_POWER_8dBm 34
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";

static void log_error_if_nonzero(const char *message, int error_code) {
  if (error_code != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
  }
}

static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init(void) {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = EXAMPLE_ESP_WIFI_SSID,
              .password = EXAMPLE_ESP_WIFI_PASS,
              /* Authmode threshold resets to WPA2 as default if password
               * matches WPA2 standards (pasword len => 8). If you want to
               * connect the device to deprecated WEP/WPA networks, Please set
               * the threshold value to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set
               * the password with length and format matching to
               * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
               */
              .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
              .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
              .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(WIFI_POWER_8dBm));

  ESP_LOGI(TAG, "wifi_init_sta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID,
             EXAMPLE_ESP_WIFI_PASS);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  ESP_LOGD(TAG,
           "Event dispatched from event loop base=%s, event_id=%" PRIi32 "",
           base, event_id);
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;

  // case MQTT_EVENT_SUBSCRIBED:
  //   ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
  //   msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
  //   ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
  //   break;
  // case MQTT_EVENT_UNSUBSCRIBED:
  //   ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
  //   break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);
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

static esp_mqtt_client_handle_t mqtt_app_start(void) {
  esp_mqtt_client_config_t mqtt_cfg = {
      .credentials =
          {
              .username = CONFIG_BROKER_USERNAME,
              .authentication.password = CONFIG_BROKER_PASSWORD,
          },
      .broker.address.uri = CONFIG_BROKER_URL,
  };

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  /* The last argument may be used to pass data to the event handler, in this
   * example mqtt_event_handler */
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 NULL);
  esp_mqtt_client_start(client);
  return client;
}

void sensor_task(void *pvParameters) {
  esp_mqtt_client_handle_t client = pvParameters;
  i2c_dev_t dev;
  memset(&dev, 0, sizeof(i2c_dev_t));

  ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, CONFIG_ROTOM_I2C_MASTER_SDA,
                                   CONFIG_ROTOM_I2C_MASTER_SCL));

#ifdef CONFIG_ROTOM_CHIP_TYPE_SI70xx
  uint64_t serial;
  si7021_device_id_t id;

  ESP_ERROR_CHECK(si7021_get_serial(&dev, &serial, false));
  ESP_ERROR_CHECK(si7021_get_device_id(&dev, &id));

  printf("Device: ");
  switch (id) {
  case SI_MODEL_SI7013:
    printf("Si7013");
    break;
  case SI_MODEL_SI7020:
    printf("Si7020");
    break;
  case SI_MODEL_SI7021:
    printf("Si7021");
    break;
  case SI_MODEL_SAMPLE:
    printf("Engineering sample");
    break;
  default:
    printf("Unknown");
  }
  printf("\nSerial number: 0x%08" PRIx32 "%08" PRIx32 "\n",
         (uint32_t)(serial >> 32), (uint32_t)serial);
#endif

  float val;
  esp_err_t res;

  /* wait for the device to boot. HTU21D sometimes fails to return data
   * at the initial reading. the datasheet does not say anything about
   * startup sequence. */
  vTaskDelay(pdMS_TO_TICKS(1000));
  int msg_id;

  while (1) {
    /* float is used in printf(). you need non-default configuration in
     * sdkconfig for ESP8266, which is enabled by default for this
     * example. see sdkconfig.defaults.esp8266
     */
    res = si7021_measure_temperature(&dev, &val);
    if (res != ESP_OK)
      printf("Could not measure temperature: %d (%s)\n", res,
             esp_err_to_name(res));
    else {
      printf("Temperature: %.2f\n", val);
      char buf[8] = {0};
      sprintf(buf, "%.2f", val);
      msg_id = esp_mqtt_client_publish(client, "rotom-heat/temperature", buf, 0,
                                       1, 0);
      ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
    }

    res = si7021_measure_humidity(&dev, &val);
    if (res != ESP_OK)
      printf("Could not measure humidity: %d (%s)\n", res,
             esp_err_to_name(res));
    else {
      printf("Humidity: %.2f\n", val);
      char buf[8] = {0};
      sprintf(buf, "%.2f", val);
      msg_id =
          esp_mqtt_client_publish(client, "rotom-heat/humidity", buf, 0, 1, 0);
      ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init();

  esp_mqtt_client_handle_t client = mqtt_app_start();

  ESP_ERROR_CHECK(i2cdev_init());

  xTaskCreatePinnedToCore(sensor_task, "test", configMINIMAL_STACK_SIZE * 8,
                          client, 5, NULL, APP_CPU_NUM);
}
