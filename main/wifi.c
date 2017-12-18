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
#include "esp_smartconfig.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "lwip/err.h"
#include "apps/sntp/sntp.h"
#include "wifi.h"


#define WIFI_SSID "Tech_D0048070"
#define WIFI_PASS "UREZYUND"

#define WIFI_NVS_NAMESPACE "wifi"

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;
const int ESPTOUCH_DONE_BIT = BIT1;
const int ESP_TOUCH_CONFIG_BIT = BIT4;
const int WIFI_NOT_SET_BIT = BIT5;

   

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

// logging tag
static const char *TAG = "wifi";

static esp_err_t event_handler(void *ctx, system_event_t *event);
void smartconfig_task(void * parm);
static esp_err_t read_wifi_credentials(char *ssid, char *passwd);
static esp_err_t save_wifi_credentials(const char *ssid, const char *passwd);



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
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    EventBits_t uxBits;
    uxBits = xEventGroupGetBits(wifi_event_group);
    if( ( uxBits & ESP_TOUCH_CONFIG_BIT ) == 0 )  // normal connect mode
    {
        char *ssid = malloc(32 * sizeof(size_t));
        char *passwd = malloc(64 * sizeof(size_t));

 
        esp_err_t err;
        err = read_wifi_credentials(ssid, passwd);

        if (err == ESP_OK) {
            wifi_sta_config_t sta;
            memcpy(sta.ssid, ssid, 32 * sizeof(size_t));
            memcpy(sta.password, passwd, 64 * sizeof(size_t));

            wifi_config_t wifi_config;
            wifi_config.sta = sta;

            ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );    
        }
        else
        {
            uxBits = xEventGroupSetBits(wifi_event_group, WIFI_NOT_SET_BIT);
            ESP_LOGI(TAG, "WiFi configuration is not set.");
            return;
        }

    }  // ESPTOUCH mode

    ESP_ERROR_CHECK( esp_wifi_start() );
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
            xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
        }
        else
        {
            // regular wifi connect mode
            esp_wifi_connect();
        }    
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





static void sc_callback(smartconfig_status_t status, void *pdata)
{
    switch (status) {
        case SC_STATUS_WAIT:
            ESP_LOGI(TAG, "SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            ESP_LOGI(TAG, "SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
            ESP_LOGI(TAG, "SC_STATUS_LINK");
            wifi_config_t *wifi_config = pdata;
            ESP_LOGI(TAG, "SSID:%s", wifi_config->sta.ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", wifi_config->sta.password);

            save_wifi_credentials((const char*)wifi_config->sta.ssid, (const char*)wifi_config->sta.password);

            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        case SC_STATUS_LINK_OVER:
            ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = { 0 };
                memcpy(phone_ip, (uint8_t* )pdata, 4);
                ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
    }
}

void smartconfig_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
    while (1) {
        uxBits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}


static esp_err_t save_wifi_credentials(const char *ssid, const char *passwd)
{
    nvs_handle my_handle;
    esp_err_t err;

    ESP_LOGI(TAG, "Saving WiFi credentials ssid: %s, pass: %s", ssid, passwd);

    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle, "ssid", ssid, strlen(ssid));
    err = nvs_set_blob(my_handle, "passwd", passwd, strlen(passwd));
    err = nvs_commit(my_handle);
    
    if (err != ESP_OK) return err;
    nvs_close(my_handle);
    
    return ESP_OK;
}


static esp_err_t read_wifi_credentials(char *ssid, char *passwd)
{
    nvs_handle my_handle;
    esp_err_t err;

    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    size_t ssid_size = 0;  // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(my_handle, "ssid", NULL, &ssid_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return err;

    size_t passwd_size = 0;  // value will default to 0, if not set yet in NVS    
    err = nvs_get_blob(my_handle, "passwd", NULL, &passwd_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return err;

    // Read previously saved blob if available
    if (ssid_size > 0 && passwd_size > 0) {
        nvs_get_blob(my_handle, "ssid", ssid, &ssid_size);
        nvs_get_blob(my_handle, "passwd", passwd, &passwd_size);
    }
    else 
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    return ESP_OK;
}