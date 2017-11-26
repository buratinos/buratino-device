/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_adc_cal.h"
#include "ds18b20.h"

#include "esp_err.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include "esp_spiffs.h"
#include <sys/unistd.h>
#include <sys/stat.h>

#include <time.h>
#include <sys/time.h>
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "apps/sntp/sntp.h"




#define V_REF 1100
#define TEMP_GPIO 14
#define ADC1_SOIL_CHANNEL (ADC1_CHANNEL_6) //GPIO 34, A2 Feather
#define ADC1_LIGHT_CHANNEL (ADC1_CHANNEL_0) //GPIO 36, A4 Feather

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID "Tech_D0048070"
#define EXAMPLE_WIFI_PASS "UREZYUND"

/* Sheduling configuration
   TODO: make configurable by the user
*/ 
#define DEEP_SLEEP_DELAY 10000  // delay between reboots, in ms
#define FREQ_TEMPERATURE 1      // read out temperatute every X reboots
#define FREQ_LIGHT 1            // read out light every X reboots
#define FREQ_SOIL 2             // read out soil every X reboots
#define FREQ_SYNC 3             // sync data to the server every X reboots

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

// logging tag
static const char *TAG = "main";

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
 RTC_DATA_ATTR static int boot_count = 0;
 RTC_DATA_ATTR static struct timeval sleep_enter_time;
 
 static void obtain_time(void);
 static void initialize_sntp(void);
 static void initialise_wifi(void);
 static esp_err_t event_handler(void *ctx, system_event_t *event);


void app_main()
{
    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);  // boot counts between deep sleep sessions

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long currentMillis = xTaskGetTickCount();
    int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
    ESP_LOGI(TAG, "Time spent in deep sleep: %dms\n", sleep_time_ms);

    ESP_LOGI(TAG, "Setting up analog channels");
    adc1_config_width(ADC_WIDTH_BIT_12);

    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 10,
      .format_if_mount_failed = true
    };
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%d)", ret);
        }
        return;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information");
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }


    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: {
            // not a deep sleep reboot

            ESP_LOGI(TAG, "Cleaning readout data files");

            struct stat st;  // TODO make in a loop
            if (stat("/spiffs/temperature.txt", &st) == 0) {
                unlink("/spiffs/temperature.txt");
            }

            if (stat("/spiffs/light.txt", &st) == 0) {
                unlink("/spiffs/light.txt");
            }

            if (stat("/spiffs/soil.txt", &st) == 0) {
                unlink("/spiffs/soil.txt");
            }
        }
        default:
            printf("Normal deep sleep reboot\n");
    }


    // perform sensor readouts
    if (boot_count % FREQ_TEMPERATURE == 0) {
        // Init temp sensor
        ESP_LOGI(TAG, "Initializing temperature sensor");
        ds18b20_init(TEMP_GPIO);

        ESP_LOGI(TAG, "Saving temperature value..");
        FILE* f = fopen("/spiffs/temperature.txt", "a");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open temperature file for writing");
            return;
        }

        float temp = ds18b20_get_temp();
        printf("Temperature at %d: %0.1f\n", sleep_time_ms, temp);
        fprintf(f, "%d, %0.2f\n", sleep_time_ms, temp);
        fclose(f);

        ESP_LOGI(TAG, "Read out data stored");
    }


    // perform sensor readouts
    if (boot_count % FREQ_LIGHT == 0) {
        esp_adc_cal_characteristics_t characteristics_tmp;
        adc1_config_channel_atten(ADC1_LIGHT_CHANNEL, ADC_ATTEN_11db);
        esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics_tmp);

        ESP_LOGI(TAG, "Saving light value..");
        FILE* f = fopen("/spiffs/light.txt", "a");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open light file for writing");
            return;
        }

        int light = adc1_to_voltage(ADC1_LIGHT_CHANNEL, &characteristics_tmp);
        
        printf("Light: %d\n", light);

        fprintf(f, "%d, %d\n", sleep_time_ms, light);
        fclose(f);

        ESP_LOGI(TAG, "Read out data stored");
    }

    // perform sensor readouts
    if (boot_count % FREQ_SOIL == 0) {
        esp_adc_cal_characteristics_t characteristics_lgt;
        adc1_config_channel_atten(ADC1_SOIL_CHANNEL, ADC_ATTEN_11db);
        esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics_lgt);

        ESP_LOGI(TAG, "Saving soil moisture value..");
        FILE* f = fopen("/spiffs/soil.txt", "a");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open soil file for writing");
            return;
        }

        currentMillis = xTaskGetTickCount();
        int soil = adc1_to_voltage(ADC1_SOIL_CHANNEL, &characteristics_lgt);
        
        printf("Soil: %d\n", soil);

        fprintf(f, "%d, %d\n", sleep_time_ms, soil);
        fclose(f);

        ESP_LOGI(TAG, "Read out data stored");
    }
    
    /*

    // SETTING UP TIME
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }
    char strftime_buf[64];

    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in New York is: %s", strftime_buf);

    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);

    */


    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    FILE* f = fopen("/spiffs/light.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[256];
    while (fgets(line, 256, f)) {
        printf("%s", line);
    }
    fclose(f);

    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");

    const int deep_sleep_sec = 500;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", deep_sleep_sec);
    esp_deep_sleep(1000000LL * deep_sleep_sec);
}


void display_chip_info() {
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d\n", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}


static void obtain_time(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    ESP_ERROR_CHECK( esp_wifi_stop() );
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
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