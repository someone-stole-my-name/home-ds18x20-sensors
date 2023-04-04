#include "esp_http_client.h"
#include "esp_sleep.h"
#include "lwip/netdb.h"
#include "wifi.h"
#include <ds18x20.h>
#include <stdio.h>

#define ST(A) #A
#define STR(A) ST(A)

#ifndef MAX_SENSOR_COUNT
#define MAX_SENSOR_COUNT 10
#endif

#ifndef TEMPERATURE_SENSOR_PIN
#define TEMPERATURE_SENSOR_PIN GPIO_NUM_17
#endif

static float temps[MAX_SENSOR_COUNT];

esp_err_t _http_event_handler(esp_http_client_event_t *evt) { return ESP_OK; }

void app_main() {
  const int wakeup_time_sec = 900;
  const wifi_sta_config_t wifi_config = {
      .ssid = STR(WIFI_SSID),
      .password = STR(WIFI_PASS),
  };

  ds18x20_addr_t addrs[MAX_SENSOR_COUNT];
  size_t sensor_count = 0;

  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << TEMPERATURE_SENSOR_PIN);
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  ESP_ERROR_CHECK(ds18x20_scan_devices(TEMPERATURE_SENSOR_PIN, addrs,
                                       MAX_SENSOR_COUNT, &sensor_count));
  printf("Found %d sensors\n", sensor_count);

  ESP_ERROR_CHECK(wifi_init(&wifi_config));

  esp_http_client_config_t config = {
      .host = STR(PUSH_GATEWAY_HOST),
      .port = PUSH_GATEWAY_PORT,
      .path = "/metrics/job/ds1820",
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
      .event_handler = _http_event_handler,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);

  ds18x20_measure_and_read_multi(TEMPERATURE_SENSOR_PIN, addrs, sensor_count,
                                 temps);
  for (int i = 0; i < sensor_count; i++) {
    char *post_data;
    char *url;

    if (0 > asprintf(&post_data,
                     "# TYPE ds1820_sensor "
                     "gauge\nds1820_sensor %f\n",
                     temps[i])) {
      free(post_data);
      goto sleep;
    }

    if (0 > asprintf(&url, "/metrics/job/ds1820/addr/%08x", (uint32_t)addrs[i])) {
      free(post_data);
      free(url);
      goto sleep;
    }

    printf("Sensor: %08x - %fC\n", (uint32_t)addrs[i], temps[i]);

    esp_http_client_set_url(client, url);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_perform(client);
    free(post_data);
    free(url);
  }

sleep:
  esp_http_client_cleanup(client);
  _wifi_deinit();
  esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
  esp_deep_sleep_start();
}
