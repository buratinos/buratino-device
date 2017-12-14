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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"


#include "utils.h"
#include "sensors.h"
#include "storage.h"
#include "wifi.h"



/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "buratino.asobolev.ru"
#define WEB_PORT 80
#define WEB_URL "http://buratino.asobolev.ru/api/v1/devices/2e52e67d-d0f5-4f87-b7b6-9aae97a42623/readouts"

/* Sheduling configuration
   TODO: make configurable by the user
*/ 
#define DEEP_SLEEP_DELAY 10     // delay between reboots, in ms
#define FREQ_SYNC 1             // sync data to the server every X reboots


 

// logging tag
static const char *TAG = "main";

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
RTC_DATA_ATTR static int boot_count = 0;
RTC_DATA_ATTR static struct timeval sleep_enter_time;


static void http_get_task(char *REQUEST);



void app_main()
{
    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);  // boot counts between deep sleep sessions

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

        char curr_time_buf[64];

        for (int i = 0; i < get_sensor_number(); i++) {

            // reading stored sensor values to a buffer
            int readout_cnt = get_readouts_count(sensors[i].code);

            if (readout_cnt == 0) {
                continue;
            }
            unsigned long *times = malloc(readout_cnt * sizeof(unsigned long));
            int *values = malloc(readout_cnt * sizeof(unsigned long));
            char *req_body = malloc(readout_cnt * 128);

            strcpy(req_body,  "[");

            for (int j=0; j < readout_cnt; j++) {
                char readout[128];
                
                time_t readout_time = sleep_enter_time.tv_sec + times[j] / 1000;
                localtime_r(&readout_time, &timeinfo);
                strftime(curr_time_buf, sizeof(curr_time_buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);

                if (j != 0) {
                    strcat(req_body, ", ");
                }

                snprintf(readout, 128, 
                    "{\"timestamp\": \"%s\", \"sensor_type\": \"%s\", \"value\": %d}", 
                    curr_time_buf, sensors[i].code, values[j]
                );

                strcat(req_body, readout);
            }

            strcat(req_body,  "]");

            // building request header
            char req_header[1024];
            snprintf(req_header, 1024, "POST " WEB_URL " HTTP/1.0\r\n"
                "Host: "WEB_SERVER"\r\n"
                "User-Agent: esp-idf/1.0 esp32\r\n"
                "Accept: application/json\r\n"
                "Connection: close\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "\r\n", strlen(req_body));

            
            char *request = malloc(strlen(req_header) + strlen(req_body)); 
            strcpy(request,  req_header);
            strcat(request,  req_body);

            ESP_LOGI(TAG, "FULL REQUEST: \n%s", request);
            
            free(times);
            free(values);
            free(req_body);
            free(request);
        }

        // SYNC data
        //xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
        //http_get_task(request);
    
        stop_wifi();
    }

    // unmount SPIFFS filesystem
    storage_close();

    //const int deep_sleep_sec = 10;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", DEEP_SLEEP_DELAY);
    esp_deep_sleep(1000000LL * DEEP_SLEEP_DELAY);
}


static void http_get_task(char *REQUEST)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    /* TODO: properly design the async calls
        Wait for the callback to set the CONNECTED_BIT in the
        event group.
    */
    //xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
    //                    false, true, portMAX_DELAY);
    //ESP_LOGI(TAG, "Connected to AP");


    int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

    if(err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return;
    }

    /* Code to print the resolved IP.
        Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        ESP_LOGE(TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return;
    }
    ESP_LOGI(TAG, "... allocated socket");

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return;
    }

    ESP_LOGI(TAG, "... connected");
    freeaddrinfo(res);

    if (write(s, REQUEST, strlen(REQUEST)) < 0) {
        ESP_LOGE(TAG, "... socket send failed");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return;
    }
    ESP_LOGI(TAG, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
            sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return;
    }
    ESP_LOGI(TAG, "... set socket receiving timeout success");

    /* Read HTTP response */
    do {
        bzero(recv_buf, sizeof(recv_buf));
        r = read(s, recv_buf, sizeof(recv_buf)-1);
        for(int i = 0; i < r; i++) {
            putchar(recv_buf[i]);
        }
    } while(r > 0);

    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
    close(s);
}
