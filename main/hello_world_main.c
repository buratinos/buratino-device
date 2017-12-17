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
#define DEEP_SLEEP_DELAY 20       // delay between reboots, in ms
#define FREQ_SYNC 100             // sync data to the server every X reboots
#define BLINK_GPIO 13
#define DEVICE_ID "2e52e67d-d0f5-4f87-b7b6-9aae97a42623"

// logging tag
static const char *TAG = "main";

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
RTC_DATA_ATTR static int boot_count = 0;
RTC_DATA_ATTR static struct timeval sleep_enter_time;


static void get_readouts_as_json(const char *sensor_type, char *json_string);


void app_main()
{
    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);  // boot counts between deep sleep sessions

    // flash red LED to indicate awake state
    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 1);

    // enable output voltage for sensors
    dac_output_enable(DAC_CHANNEL_2);
    dac_output_voltage(DAC_CHANNEL_2, 255);

    struct timeval now;

    gettimeofday(&now, NULL);
    unsigned long currentMillis = xTaskGetTickCount();
    unsigned long sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
    ESP_LOGI(TAG, "Sleep enter time: %f\n", (double)sleep_enter_time.tv_sec);
    ESP_LOGI(TAG, "Time spent in deep sleep: %lu ms\n", sleep_time_ms);

    // init SPIFFS filesystem
    storage_init();

    // init NVS flash storage
    ESP_ERROR_CHECK( nvs_flash_init() );

    // init sensor settings
    sensor_settings_t *sensors = malloc(sizeof(sensor_settings_t) * get_sensor_number());
    sensor_settings_init(sensors);


    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            if (wakeup_pin_mask != 0) {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                ESP_LOGI(TAG, "WAKE UP FROM GPIO %d\n", pin);

            } else {
                ESP_LOGI(TAG, "WAKE UP FROM GPIO\n");
            }
            break;
        }

        case ESP_SLEEP_WAKEUP_UNDEFINED: {
            // not a deep sleep reboot
            ESP_LOGI(TAG, "Cleaning readout data files");

            for (int i = 0; i < get_sensor_number(); i++) {
                flush_readouts(sensors[i].code);
            }
        }

        default:
            ESP_LOGI(TAG, "Normal deep sleep reboot\n");
    }

    // perform sensor readouts
    for (int i = 0; i < get_sensor_number(); i++) {
        if (boot_count % sensors[i].read_frequency == 0) {
            dump_readout(sensors[i].code, sleep_time_ms, sensors[i].read());    
        }
    }


    struct tm timeinfo;
    // sync data to the cloud
    if (boot_count % FREQ_SYNC == 0) {
        time_t time_now;
        time(&time_now);
        localtime_r(&time_now, &timeinfo);

        initialise_wifi();

        // Is time set? If not, tm_year will be (1970 - 1900).
        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGI(TAG, "Time is not set yet. Getting time over NTP.");
            
            obtain_time();
        }
 
        // update sleep enter time
        struct timeval act_time;
        gettimeofday(&act_time, NULL);
        sleep_enter_time.tv_sec = act_time.tv_sec - sleep_time_ms / 1000;

        for (int i = 0; i < get_sensor_number(); i++) {
            int readout_cnt = get_readouts_count(sensors[i].code);
            
            if (readout_cnt == 0) {
                continue;
            }

            // reading stored sensor values to a buffer
            char *json_string = malloc(readout_cnt * 128);
            get_readouts_as_json(sensors[i].code, json_string);

            char *request = malloc(1024 + strlen(json_string)); 
            build_POST_request(json_string, request);

            ESP_LOGI(TAG, "FULL REQUEST: \n%s", request);

            // SYNC data
            //xTaskCreate(&http_POST, "http_POST", 4096, NULL, 5, NULL);
            //int status = http_POST(request);

            //if (status == 201) {
            //    ESP_LOGI(TAG, "Sync successful, cleaning stored readouts for %s", sensors[i].code);
            //    flush_readouts(sensors[i].code);
            //}

            free(json_string);
            free(request);
        }
    
        stop_wifi();
    }

    // unmount SPIFFS filesystem
    storage_close();

    const int ext_wakeup_pin_1 = 25;
    const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;

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


static void get_readouts_as_json(const char *sensor_type, char *json_string)
{
    int readout_cnt = get_readouts_count(sensor_type);

    char curr_time_buf[64];
    struct tm timeinfo;
    unsigned long *times = malloc(readout_cnt * sizeof(unsigned long));
    int *values = malloc(readout_cnt * sizeof(unsigned long));

    get_readouts(sensor_type, times, values);

    strcpy(json_string,  "[");

    for (int j=0; j < readout_cnt; j++) {
        char readout[128];
        
        time_t readout_time = sleep_enter_time.tv_sec + times[j] / 1000;
        localtime_r(&readout_time, &timeinfo);
        strftime(curr_time_buf, sizeof(curr_time_buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);

        if (j != 0) {
            strcat(json_string, ", ");
        }

        snprintf(readout, 128, 
            "{\"device\": \"%s\", \"timestamp\": \"%s\", \"sensor_type\": \"%s\", \"value\": %d}", 
            DEVICE_ID, curr_time_buf, sensor_type, values[j]
        );

        strcat(json_string, readout);
    }

    strcat(json_string,  "]");

    free(times);
    free(values);
}