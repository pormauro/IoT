#include "http_api.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "config_store.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTPAPI";
static config_t *g_cfg;

static esp_err_t get_status(httpd_req_t *req) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"fw\":\"%s\",\"provisioned\":%s}",
        g_cfg->device_id, APP_VERSION_STR, g_cfg->provisioned ? "true":"false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t get_config(httpd_req_t *req) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
      "{\"mode\":%d,\"services\":{\"https_api\":%s,\"mqtt\":%s},\"security\":{\"require_admin_token\":%s}}",
      (int)g_cfg->mode,
      g_cfg->services.https_api?"true":"false",
      g_cfg->services.mqtt?"true":"false",
      g_cfg->sec.require_admin_token?"true":"false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t put_config(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t post_ota(httpd_req_t *req) {
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_send(req, NULL, 0);
}

void http_api_start(config_t *cfg) {
#if CONFIG_ENABLE_HTTP_API
    g_cfg = cfg;
    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    conf.server_port = 80;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &conf) == ESP_OK) {
        httpd_uri_t u1 = { .uri="/api/v1/status", .method=HTTP_GET, .handler=get_status };
        httpd_uri_t u2 = { .uri="/api/v1/config", .method=HTTP_GET, .handler=get_config };
        httpd_uri_t u3 = { .uri="/api/v1/config", .method=HTTP_PUT, .handler=put_config };
        httpd_uri_t u4 = { .uri="/api/v1/ota",    .method=HTTP_POST, .handler=post_ota };
        httpd_register_uri_handler(server, &u1);
        httpd_register_uri_handler(server, &u2);
        httpd_register_uri_handler(server, &u3);
        httpd_register_uri_handler(server, &u4);
        ESP_LOGI(TAG, "HTTP API iniciada en puerto %d", conf.server_port);
    } else {
        ESP_LOGE(TAG, "No se pudo iniciar httpd");
    }
#else
    (void)cfg;
#endif
}
