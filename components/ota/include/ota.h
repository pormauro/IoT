#pragma once
#include "config_store.h"
void ota_init(const config_t *cfg);
esp_err_t ota_trigger(const char *url);
