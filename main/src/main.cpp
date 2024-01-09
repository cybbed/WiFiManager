#include "wifi_manager.h"
#include "driver/gpio.h"

#define TAG "main"

#define ESP32_DK_LED_BITMASK (1 << 2) 
#define ESP32_DK_LED_PIN     GPIO_NUM_2

void led_init(void) {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = ESP32_DK_LED_BITMASK;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
}

void wifi_event_receive(WiFiManager::wifi_events_t event) {
    ESP_LOGI(TAG, "Wi-Fi event %s received", WiFiManager::wifi_event_to_string(event).c_str());
    if (event == WiFiManager::WIFI_CONNECTED) {
        gpio_set_level(ESP32_DK_LED_PIN, 1);
    } else {
        gpio_set_level(ESP32_DK_LED_PIN, 0);
    }
}

extern "C" void app_main(void)
{
    led_init();

    WiFiManager wifi;
    WiFiManager::wifi_error_codes_t ret = wifi.register_event(WiFiManager::WIFI_CONNECTED, wifi_event_receive);
    if (ret != WiFiManager::WIFI_SUCCESS) {
        ESP_LOGE(TAG, "Failed to register WiFi callback. Error %u occurred", ret);
        assert(0);
    }

    ret = wifi.register_event(WiFiManager::WIFI_DISCONNECTED, wifi_event_receive);
    if (ret != WiFiManager::WIFI_SUCCESS) {
        ESP_LOGE(TAG, "Failed to register WiFi callback. Error %u occurred", ret);
        assert(0);
    }

    ret = wifi.initialize();
    if (ret != WiFiManager::WIFI_SUCCESS) {
        ESP_LOGE(TAG, "Failed to initialize WiFi. Error %u occurred", ret);
        assert(0);
    }

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
