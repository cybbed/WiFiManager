#include <functional>
#include <map>
#include <string>
#include <vector>

#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

class WiFiManager {
public:
  typedef enum { WIFI_SUCCESS, WIFI_FAILURE } wifi_error_codes_t;

  typedef enum { WIFI_CONNECTED, WIFI_DISCONNECTED, WIFI_LAST } wifi_events_t;

  static std::string wifi_event_to_string(wifi_events_t event);

  WiFiManager() : task_handle(0), state(WIFI_MNGR_READY){};
  wifi_error_codes_t initialize();
  wifi_error_codes_t register_event(wifi_events_t event, std::function<void(wifi_events_t event)>);

private:
  static constexpr char *TAG = "WIFI";

  const static inline std::vector<std::string> wifi_events_mapping = {"WIFI_CONNECTED", "WIFI_DISCONNECTED"};

  typedef enum {
    WIFI_CONNECTED_EVENT = BIT0,
    WIFI_DISCONNECTED_EVENT = BIT1,
    WIFI_IP_ASSIGNED_EVENT = BIT2,
    WIFI_SMARTCONFIG_IP_SET_EVENT = BIT3,
    WIFI_SMARTCONFIG_DONE_EVENT = BIT4,
    WIFI_LAST_EVENT = 0xFFFF
  } wifi_sta_events_t;

  typedef enum {
    WIFI_MNGR_READY,
    WIFI_MNGR_CONNECTING,
    WIFI_MNGR_CONNECTED,
    WIFI_MNGR_DISCONNECTED,
    WIFI_MNGR_PROVISIONING
  } wifi_mgr_states_t;

  EventGroupHandle_t event_group;
  static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  TaskHandle_t task_handle;
  static void wifi_task_handler(void *parameter);

  std::map<wifi_events_t, std::function<void(wifi_events_t event)>> callbacks;

  wifi_mgr_states_t state;

  std::vector<uint8_t> ssid;
  std::vector<uint8_t> password;
  std::vector<uint8_t> bssid;
};