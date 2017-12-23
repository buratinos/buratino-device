/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
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

#include "esp_event_loop.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "utils.h"
#include "sensors.h"
#include "storage.h"
#include "wifi.h"
#include "http.h"
#include <driver/dac.h>


/* Sheduling configuration
   TODO: make configurable by the user
*/ 
#define DEEP_SLEEP_DELAY 10       // delay between reboots, in ms
#define FREQ_SYNC 2             // sync data to the server every X reboots
#define BLINK_GPIO 13
#define EXT_WAKEUP_GPIO 25        // GPIO 25 / A1
#define DEVICE_ID "2e52e67d-d0f5-4f87-b7b6-9aae97a42623"
#define WIFI_CONNECT_TIMEOUT 30000  // in ms
#define ESPTOUCH_CONNECT_TIMEOUT 60000  // in ms
#define HTTP_REQUEST_TIMEOUT 20000  // in ms


// logging tag
static const char *TAG = "main";

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
RTC_DATA_ATTR static int boot_count = 0;
RTC_DATA_ATTR static struct timeval sleep_enter_time;


static void get_readouts_as_json(const char *sensor_type, char *json_string);
static void sync_over_http(sensor_settings_t sensor);
static void blink_task(void *pvParameter);
static void blink_success();
static void smart_config_routine();
static void read_sensor_routine();
static void sync_data_routine();


EventBits_t uxBits;
static EventGroupHandle_t wf_event_group;
unsigned long sleep_time_ms;


void app_main()
{
    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);  // boot counts between deep sleep sessions

    // flash red LED to indicate awake state
    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 1);

    // enable output voltage for sensors
    dac_output_enable(DAC_CHANNEL_2);  // GPIO 26 / A0
    dac_output_voltage(DAC_CHANNEL_2, 255);

    // compute time spent in deep sleep 
    struct timeval now;
    gettimeofday(&now, NULL);
    sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
    ESP_LOGI(TAG, "Sleep enter time: %f\n", (double)sleep_enter_time.tv_sec);
    ESP_LOGI(TAG, "Time spent in deep sleep: %lu ms\n", sleep_time_ms);

    // init SPIFFS filesystem
    storage_init();

    // init NVS flash storage
    ESP_ERROR_CHECK( nvs_flash_init() );

    // init sensor settings
    sensor_settings_init();

    wf_event_group = xEventGroupCreate();


    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT1: {
            ESP_LOGI(TAG, "WAKE UP FROM GPIO\n");

            smart_config_routine();
            break;
        }

        case ESP_SLEEP_WAKEUP_UNDEFINED: { // not a deep sleep reboot, most probably time is reset
            ESP_LOGI(TAG, "Cleaning readout data files");

            for (int i = 0; i < sizeof(sensors)/sizeof(sensor_settings_t); i++) {
                flush_readouts(sensors[i].code);
            }
        }

        default:
            // perform sensor readouts TODO get via xQueue?
            read_sensor_routine();
            
            // sync data to the cloud
            if (boot_count % FREQ_SYNC == 0) {
                sync_data_routine();
            }
    }

    // disconnect WiFi
    if( ( uxBits & CONNECTED_BIT ) != 0 )
    {
        stop_wifi();
    }

    // unmount SPIFFS filesystem
    storage_close();

    const int ext_wakeup_pin_1 = 25;
    const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;

    // enable wakeup from EXT1
    ESP_LOGI(TAG, "Enabling EXT1 wakeup on pins GPIO%d\n", ext_wakeup_pin_1);
    esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    // disable sensor power
    dac_output_voltage(DAC_CHANNEL_2, 0);
    dac_output_disable(DAC_CHANNEL_2);

    // turn off red LED
    gpio_set_level(BLINK_GPIO, 0);

    //const int deep_sleep_sec = 10;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", DEEP_SLEEP_DELAY);
    esp_deep_sleep(1000000LL * DEEP_SLEEP_DELAY);
}


static void smart_config_routine()
{
    uxBits = xEventGroupSetBits(wf_event_group, ESP_TOUCH_CONFIG_BIT);
    initialise_wifi(wf_event_group);
    
    TaskHandle_t xBlinkHandle = NULL;
    xTaskCreate(&blink_task, "blink_task", configMINIMAL_STACK_SIZE, NULL, 5, &xBlinkHandle);
    uxBits = xEventGroupWaitBits(wf_event_group, ESPTOUCH_DONE_BIT, false, true, ESPTOUCH_CONNECT_TIMEOUT / portTICK_PERIOD_MS);
    
    if( xBlinkHandle != NULL )
    {
        vTaskDelete( xBlinkHandle );
    }

    if( ( uxBits & ESPTOUCH_DONE_BIT ) == 0 )
    {
        ESP_LOGE(TAG, "ESPTOUCH failed due to timeout, check settings and try again.");
    }
    
    if( ( uxBits & ESPTOUCH_DONE_BIT ) != 0 )
    {
        blink_success();
    }
}


static void read_sensor_routine()
{
    for (int i = 0; i < sizeof(sensors)/sizeof(sensor_settings_t); i++) {
        if (boot_count % sensors[i].read_frequency == 0) {
            dump_readout(sensors[i].code, sleep_time_ms, sensors[i].read());    
        }
    }
}


static void sync_data_routine()
{
    struct tm timeinfo;
    time_t time_now;
    time(&time_now);
    localtime_r(&time_now, &timeinfo);

    initialise_wifi(wf_event_group);
    uxBits = xEventGroupWaitBits(
        wf_event_group, 
        CONNECTED_BIT | WIFI_NOT_SET_BIT, 
        false, false, 
        WIFI_CONNECT_TIMEOUT / portTICK_PERIOD_MS
    );

    if( ( uxBits & CONNECTED_BIT ) != 0 )
    {
        // Is time set? If not, tm_year will be (1970 - 1900).
        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGI(TAG, "Time is not set yet. Getting time over NTP.");
            obtain_time();  // TODO do something if failed

            uxBits = xEventGroupWaitBits(wf_event_group, TIME_SET_BIT, false, false, HTTP_REQUEST_TIMEOUT / portTICK_PERIOD_MS);
            if( ( uxBits & TIME_SET_BIT ) == 0 ) {
                ESP_LOGE(TAG, "Time set request timeout.");                
                return;  // can't sync readout data with non-absolute timestamps
            }
        }

        // update sleep enter time
        struct timeval act_time;
        gettimeofday(&act_time, NULL);
        sleep_enter_time.tv_sec = act_time.tv_sec - sleep_time_ms / 1000;

        for (int i = 0; i < sizeof(sensors)/sizeof(sensor_settings_t); i++) {
            sync_over_http(sensors[i]);
        }
    }
    else if( ( uxBits & WIFI_NOT_SET_BIT ) != 0 ) 
    {
        ESP_LOGE(TAG, "WiFi not set, try to use ESPTOUCH via button.");
    }
    else
    {
        ESP_LOGE(TAG, "WiFi timeout - unable to connect within %d secs", WIFI_CONNECT_TIMEOUT / 1000);
    }
    stop_wifi();
}


static void sync_over_http(sensor_settings_t sensor)
{
    int readout_cnt = get_readouts_count(sensor.code);
    
    if (readout_cnt == 0) {
        return;
    }

    // reading stored sensor values to a buffer
    char *json_string = malloc(readout_cnt * 128);
    get_readouts_as_json(sensor.code, json_string);

    char *request = malloc(1024 + strlen(json_string)); 
    build_POST_request(json_string, request);

    ESP_LOGI(TAG, "FULL REQUEST: \n%s", request);

    // SYNC data
    //xTaskCreate(&http_POST, "http_POST", 4096, NULL, 5, NULL);
    //int status = http_POST(request);

    //if (status == 201) {
    //    ESP_LOGI(TAG, "Sync successful, cleaning stored readouts for %s", sensor.code);
    //    flush_readouts(sensor.code);
    //}

    free(json_string);
    free(request);
}


static void get_readouts_as_json(const char *sensor_type, char *json_string)
{
    int readout_cnt = get_readouts_count(sensor_type);

    char curr_time_buf[64];
    struct tm timeinfo;
    unsigned long *times = malloc(readout_cnt * sizeof(unsigned long));
    int *values = malloc(readout_cnt * sizeof(int));

    get_readouts(sensor_type, times, values);

    strcpy(json_string,  "[");

    for (int j=0; j < readout_cnt; j++) {
        char readout[256];
        
        time_t readout_time = sleep_enter_time.tv_sec + times[j] / 1000;
        localtime_r(&readout_time, &timeinfo);
        strftime(curr_time_buf, sizeof(curr_time_buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);

        if (j != 0) {
            strcat(json_string, ", ");
        }

        snprintf(readout, 256, 
            "{\"device\": \"%s\", \"timestamp\": \"%s\", \"sensor_type\": \"%s\", \"value\": %d}", 
            DEVICE_ID, curr_time_buf, sensor_type, values[j]
        );

        strcat(json_string, readout);
    }

    strcat(json_string,  "]");

    free(times);
    free(values);
}


void blink_task(void *pvParameter)
{
    /* Blink the available red LED
     */
     int blink_duration = 700;

     while(1) {
        /* Blink off (output low) */
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(blink_duration / portTICK_PERIOD_MS);
        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(blink_duration / portTICK_PERIOD_MS);
    }
}


static void blink_success()
{
    int blink_duration = 300;

    for (int i = 0; i < 3; i++)  {
        /* Blink off (output low) */
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(blink_duration / portTICK_PERIOD_MS);
        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(blink_duration / portTICK_PERIOD_MS);
    }
}