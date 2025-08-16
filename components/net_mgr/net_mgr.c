#include "net_mgr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char *TAG = "NET";
static EventGroupHandle_t s_ev;
#define EV_CONNECTED BIT0

static void on_ip(void* arg, esp_event_base_t base, int32_t id, void* data) {
    xEventGroupSetBits(s_ev, EV_CONNECTED);
}

void net_mgr_init(const config_t *cfg) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_ENABLE_WIFI
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wicfg));
    wifi_config_t wcfg = {0};
    strncpy((char*)wcfg.sta.ssid, cfg->wifi.ssid, sizeof(wcfg.sta.ssid));
    strncpy((char*)wcfg.sta.password, cfg->wifi.pass, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
#endif

#if CONFIG_ENABLE_ETH
    // TODO: inicializar Ethernet según hardware
#endif

    s_ev = xEventGroupCreate();
}

bool net_mgr_is_connected(void) {
    return (xEventGroupGetBits(s_ev) & EV_CONNECTED) != 0;
}
