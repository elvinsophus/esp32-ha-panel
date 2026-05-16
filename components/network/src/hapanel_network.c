#include "hapanel_network.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "hapanel_network";

static hapanel_runtime_t *network_runtime;
static bool netif_ready;
static bool wifi_started;
static esp_netif_t *sta_netif;

static void set_wifi_status(const char *value, hapanel_system_level_t level)
{
    hapanel_runtime_set_status(network_runtime, HAPANEL_SYSTEM_WIFI, value, level);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        set_wifi_status("Connecting", HAPANEL_SYSTEM_LEVEL_PENDING);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi connect failed: %s", esp_err_to_name(err));
            set_wifi_status("Connect failed", HAPANEL_SYSTEM_LEVEL_WARNING);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        set_wifi_status("Disconnected", HAPANEL_SYSTEM_LEVEL_OFFLINE);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        set_wifi_status("Connected", HAPANEL_SYSTEM_LEVEL_OK);
    }
}

static bool has_configured_ssid(void)
{
    return strlen(CONFIG_HAPANEL_WIFI_SSID) > 0;
}

esp_err_t hapanel_network_start(hapanel_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_runtime = runtime;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        set_wifi_status("Netif error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        set_wifi_status("Event error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return err;
    }

    if (!netif_ready) {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == NULL) {
            ESP_LOGE(TAG, "failed to create default Wi-Fi STA netif");
            set_wifi_status("Netif error", HAPANEL_SYSTEM_LEVEL_ERROR);
            return ESP_FAIL;
        }
        netif_ready = true;
    }

    if (!wifi_started) {
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&init_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
            set_wifi_status("Init error", HAPANEL_SYSTEM_LEVEL_ERROR);
            return err;
        }

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            wifi_event_handler,
                                                            NULL,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            wifi_event_handler,
                                                            NULL,
                                                            NULL));
        wifi_started = true;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (!has_configured_ssid()) {
        ESP_LOGI(TAG, "Wi-Fi credentials are not configured");
        set_wifi_status("Not configured", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_HAPANEL_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_HAPANEL_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        set_wifi_status("Start error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return err;
    }

    set_wifi_status("Connecting", HAPANEL_SYSTEM_LEVEL_PENDING);
    return ESP_OK;
}
