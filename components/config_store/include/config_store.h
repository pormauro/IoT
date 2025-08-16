#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool dhcp;
    char ip[16], gw[16], mask[16], dns1[16], dns2[16];
} ip4_t;

typedef struct {
    char ssid[32];
    char pass[64];
    bool dhcp;
    ip4_t static_ip;
} wifi_cfg_t;

typedef struct {
    bool dhcp;
    ip4_t static_ip;
} eth_cfg_t;

typedef struct {
    bool https_api;
    bool mqtt;
    char mqtt_url[96];
    char mqtt_user[32];
    char mqtt_pass[64];
    bool modbus_tcp_server;
    bool ble_admin_enabled;
} services_t;

typedef struct {
    bool require_admin_token;
    char admin_token[64];
    bool allow_ble_after_provision;
} security_t;

typedef enum { NET_WIFI=0, NET_ETH=1 } net_mode_t;

typedef struct {
    uint32_t version;
    char device_id[32];
    net_mode_t mode;
    wifi_cfg_t wifi;
    eth_cfg_t  eth;
    services_t services;
    security_t sec;
    bool provisioned;
    uint32_t crc32;
} config_t;

void     config_store_init(void);
bool     config_store_load(config_t *cfg);
bool     config_store_save(const config_t *cfg);
void     config_store_defaults(config_t *cfg);
uint32_t config_crc32(const void *data, size_t len);
