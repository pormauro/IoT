#include "security.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

static void gen_device_id(char out[32]) {
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 32, "CTRL-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void gen_token(char out[64]) {
    static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (int i=0;i<32;i++) out[i] = alphabet[esp_random() % 62];
    out[32] = 0;
}

void security_init(config_t *cfg) {
    if (cfg->device_id[0] == 0) gen_device_id(cfg->device_id);
    if (cfg->sec.require_admin_token && cfg->sec.admin_token[0] == 0) {
        gen_token(cfg->sec.admin_token);
    }
}
