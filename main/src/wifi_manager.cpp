#include <stdlib.h>
#include <string.h>

#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_manager.h"

void WiFiManager::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  WiFiManager *obj = reinterpret_cast<WiFiManager *>(arg);

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    xEventGroupSetBits(obj->event_group, WiFiManager::WIFI_CONNECTED_EVENT);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
    ESP_LOGW(TAG, "STA stop");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    ESP_LOGW(TAG, "STA connected");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupSetBits(obj->event_group, WiFiManager::WIFI_DISCONNECTED_EVENT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(obj->event_group, WiFiManager::WIFI_IP_ASSIGNED_EVENT);
  } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
    ESP_LOGI(TAG, "Scan done");
  } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
    ESP_LOGI(TAG, "Found channel");
  } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
    ESP_LOGI(TAG, "Got SSID and password");
    smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
    obj->ssid.assign(evt->ssid, evt->ssid + 32);
    obj->password.assign(evt->password, evt->password + 64);
    if (evt->bssid_set == true) {
      obj->bssid.assign(evt->bssid, evt->bssid + 6);
    }
    xEventGroupSetBits(obj->event_group, WiFiManager::WIFI_SMARTCONFIG_IP_SET_EVENT);
  } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
    ESP_LOGI(TAG, "SC ack done");
    xEventGroupSetBits(obj->event_group, WIFI_SMARTCONFIG_DONE_EVENT);
  }
}

void WiFiManager::wifi_task_handler(void *parameter) {
  WiFiManager *obj = reinterpret_cast<WiFiManager *>(parameter);

  int s_retry_num = 0;

  ESP_LOGI(TAG, "WiFi handler started");

  while (1) {
    EventBits_t ux_bits = xEventGroupWaitBits(obj->event_group, 0xFFFF, true, false, portMAX_DELAY);
    auto wifi_event = static_cast<wifi_sta_events_t>(ux_bits);
    switch (wifi_event) {
    case WIFI_CONNECTED_EVENT: {
      ESP_LOGI(TAG, "WiFi Connected to STA");

      wifi_config_t wifi_config;
      esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
      if (ret == ESP_OK and strlen((const char *)wifi_config.sta.ssid)) {
        ESP_LOGI(TAG, "Wifi configuration already stored in flash partition called NVS");
        ESP_LOGI(TAG, "%s", wifi_config.sta.ssid);
        ESP_LOGI(TAG, "%s", wifi_config.sta.password);

        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "%u. Error %u", __LINE__, ret);
          continue;
        }

        ESP_ERROR_CHECK(esp_wifi_connect());

        obj->state = WIFI_MNGR_CONNECTING;
      } else {
        ESP_LOGI(TAG, "Wifi configuration not found in flash partition called NVS. Error %u", ret);
        ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
        smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

        obj->state = WIFI_MNGR_PROVISIONING;
      }
    } break;

    case WIFI_DISCONNECTED_EVENT: {
      ESP_LOGI(TAG, "WiFi disconnected from AP");

      if (s_retry_num < 20) {
        ESP_ERROR_CHECK(esp_wifi_connect());
        s_retry_num++;
        ESP_LOGI(TAG, "...retring connecting to the AP (%u)", s_retry_num);
      } else {
        s_retry_num = 0;
        ESP_LOGI(TAG, "Failed to connect to the AP. Start SmartConfig");

        auto callback = obj->callbacks[WIFI_DISCONNECTED];
        if (callback) {
          callback(WIFI_DISCONNECTED);
        }

        esp_smartconfig_stop();
        ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
        smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

        obj->state = WIFI_MNGR_PROVISIONING;
      }
    } break;

    case WIFI_IP_ASSIGNED_EVENT: {
      ESP_LOGI(TAG, "IP address assigned");
      if (obj->state == WIFI_MNGR_CONNECTING or obj->state == WIFI_MNGR_PROVISIONING) {
        auto callback = obj->callbacks[WIFI_CONNECTED];
        if (callback) {
          callback(WIFI_CONNECTED);
        }

        s_retry_num = 0;

        obj->state = WIFI_MNGR_CONNECTED;
      }
    } break;

    case WIFI_SMARTCONFIG_IP_SET_EVENT: {
      wifi_config_t wifi_config;
      memcpy(wifi_config.sta.ssid, obj->ssid.data(), sizeof(wifi_config.sta.ssid));
      memcpy(wifi_config.sta.password, obj->password.data(), sizeof(wifi_config.sta.password));
      if (obj->bssid.empty() == false) {
        wifi_config.sta.bssid_set = true;
        memcpy(wifi_config.sta.bssid, obj->bssid.data(), sizeof(wifi_config.sta.bssid));
      }
      ESP_LOG_BUFFER_HEX(TAG, wifi_config.sta.ssid, strlen((char *)wifi_config.sta.ssid));
      ESP_LOG_BUFFER_HEX(TAG, wifi_config.sta.password, strlen((char *)wifi_config.sta.password));

      ESP_ERROR_CHECK(esp_wifi_disconnect());
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
      ESP_ERROR_CHECK(esp_wifi_connect());
    } break;

    case WIFI_SMARTCONFIG_DONE_EVENT: {
      ESP_LOGI(TAG, "smartconfig over");
      esp_smartconfig_stop();
    } break;

    default: {
      ESP_LOGI(TAG, "WiFi event %d", wifi_event);
    } break;
    }
  }
}

WiFiManager::wifi_error_codes_t WiFiManager::initialize() {
  esp_err_t error = nvs_flash_init();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      error = nvs_flash_init();
      if (error != ESP_OK) {
        ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
        return WIFI_FAILURE;
      }
    } else {
      ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
      return WIFI_FAILURE;
    }
  }

  error = esp_netif_init();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  event_group = xEventGroupCreate();

  error = esp_event_loop_create_default();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (!sta_netif) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  error = esp_wifi_init(&cfg);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  error = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  error = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  error = esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, event_handler, this);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  error = esp_wifi_set_mode(WIFI_MODE_STA);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  error = esp_wifi_start();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%u. Error %u", __LINE__, error);
    return WIFI_FAILURE;
  }

  xTaskCreate(&WiFiManager::wifi_task_handler, "NAME", 4096, this, tskIDLE_PRIORITY, &task_handle);

  return WIFI_SUCCESS;
}

WiFiManager::wifi_error_codes_t
WiFiManager::register_event(wifi_events_t event, std::function<void(wifi_events_t event)> callback) {
  if (event >= WIFI_LAST) {
    return WIFI_FAILURE;
  }
  callbacks[event] = callback;
  return WIFI_SUCCESS;
}

std::string WiFiManager::wifi_event_to_string(wifi_events_t event) {
  if (event > wifi_events_mapping.size()) {
    return "NA";
  }
  return wifi_events_mapping[event];
}