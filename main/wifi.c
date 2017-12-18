#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_attr.h"
 
#include "lwip/err.h"
#include "apps/sntp/sntp.h"
#include "wifi.h"


#define WIFI_SSID "Tech_D0048070"
#define WIFI_PASS "UREZYUND"

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;
const int ESP_TOUCH_CONFIG_BIT = BIT4;
   

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

// logging tag
static const char *TAG = "wifi";

static esp_err_t event_handler(void *ctx, system_event_t *event);


void obtain_time()
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
    // wait for time to be set
    time_t time_now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&time_now);
        localtime_r(&time_now, &timeinfo);
    }

    // update 'time_now' variable with current time
    time(&time_now);

    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "EST5EDT, M3.2.0/2, M11.1.0", 1);  // TODO get proper TZ
    tzset();
    localtime_r(&time_now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in New York is: %s", strftime_buf);
}


void initialise_wifi(EventGroupHandle_t event_group)
{
    tcpip_adapter_init();
    wifi_event_group = event_group;
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    /* Waiting for connection */
    //xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    EventBits_t uxBits;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        uxBits = xEventGroupGetBits(wifi_event_group);
        if( ( uxBits & ESP_TOUCH_CONFIG_BIT ) != 0 )
        {
            // ESPTOUCH mode is enabled
            ESP_LOGI(TAG, "ESPTOUCH BIT IS SET !!!!!!!!");
        }

        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void stop_wifi()
{
    ESP_ERROR_CHECK( esp_wifi_stop() );
}