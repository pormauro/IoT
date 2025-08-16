#include "mqtt_client_mod.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT";

void mqtt_client_start(const config_t *cfg) {
#if CONFIG_ENABLE_MQTT
    if (!cfg->services.mqtt || cfg->services.mqtt_url[0]==0) {
        ESP_LOGW(TAG, "MQTT deshabilitado o sin URL");
        return;
    }
    esp_mqtt_client_config_t c = {
        .broker = { .address.uri = cfg->services.mqtt_url },
    };
    esp_mqtt_client_handle_t h = esp_mqtt_client_init(&c);
    if (!h) { ESP_LOGE(TAG, "mqtt init fallo"); return; }
    ESP_ERROR_CHECK(esp_mqtt_client_start(h));
    ESP_LOGI(TAG, "MQTT conectando a %s", cfg->services.mqtt_url);
#else
    (void)cfg;
#endif
}
