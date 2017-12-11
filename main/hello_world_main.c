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

#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "apps/sntp/sntp.h"
#include "utils.h"




#define V_REF 1100
#define TEMP_GPIO 14
#define ADC1_SOIL_CHANNEL (ADC1_CHANNEL_6) //GPIO 34, A2 Feather
#define ADC1_LIGHT_CHANNEL (ADC1_CHANNEL_0) //GPIO 36, A4 Feather

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define WIFI_SSID "mywifissid"
*/
#define WIFI_SSID "Tech_D0048070"
#define WIFI_PASS "UREZYUND"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "buratino.asobolev.ru"
#define WEB_PORT 80
#define WEB_URL "http://buratino.asobolev.ru/api/v1/devices/2e52e67d-d0f5-4f87-b7b6-9aae97a42623/readouts"

/* Sheduling configuration
   TODO: make configurable by the user
*/ 
#define DEEP_SLEEP_DELAY 10     // delay between reboots, in ms
#define FREQ_TEMPERATURE 1      // read out temperatute every X reboots
#define FREQ_LIGHT 1            // read out light every X reboots
#define FREQ_SOIL 2             // read out soil every X reboots
#define FREQ_SYNC 1             // sync data to the server every X reboots

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
 
 int read_adc1_value(int ADC1_CHANNEL);
 static void http_get_task(char* REQUEST);
 static void initialize_sntp(void);
 static void initialise_wifi(void);
 static esp_err_t event_handler(void *ctx, system_event_t *event);


void app_main()
{
    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);  // boot counts between deep sleep sessions

    struct timeval now;
    struct tm timeinfo;
    gettimeofday(&now, NULL);
    unsigned long currentMillis = xTaskGetTickCount();
    unsigned long sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
    ESP_LOGI(TAG, "Sleep enter time: %f\n", (double)sleep_enter_time.tv_sec);
    ESP_LOGI(TAG, "Time spent in deep sleep: %lu ms\n", sleep_time_ms);

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
        printf("Temperature at %lu: %0.1f\n", sleep_time_ms, temp);
        fprintf(f, "%lu %0.2f\n", sleep_time_ms, temp);
        fclose(f);

        ESP_LOGI(TAG, "Read out data stored");
    }


    // perform sensor readouts
    if (boot_count % FREQ_LIGHT == 0) {
        //esp_adc_cal_characteristics_t characteristics_tmp;
        //adc1_config_channel_atten(ADC1_LIGHT_CHANNEL, ADC_ATTEN_11db);
        //esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics_tmp);

        ESP_LOGI(TAG, "Saving light value..");
        FILE* f = fopen("/spiffs/light.txt", "a");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open light file for writing");
            return;
        }

        //int light = adc1_to_voltage(ADC1_LIGHT_CHANNEL, &characteristics_tmp);
        int light = read_adc1_value(ADC1_LIGHT_CHANNEL);
        
        printf("Light: %d\n", light);

        fprintf(f, "%lu %d\n", sleep_time_ms, light);
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

        int soil = adc1_to_voltage(ADC1_SOIL_CHANNEL, &characteristics_lgt);
        
        printf("Soil: %d\n", soil);

        fprintf(f, "%lu %d\n", sleep_time_ms, soil);
        fclose(f);

        ESP_LOGI(TAG, "Read out data stored");
    }

    
    ESP_ERROR_CHECK( nvs_flash_init() );


    // sync data to the cloud
    if (boot_count % FREQ_SYNC == 0) {
        time_t time_now;
        time(&time_now);
        localtime_r(&time_now, &timeinfo);

        initialise_wifi();
        /* Waiting for connection */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

        // Is time set? If not, tm_year will be (1970 - 1900).
        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGI(TAG, "Time is not set yet. Getting time over NTP.");
            
            initialize_sntp();
            
            // wait for time to be set
            time_t time_now = 0;
            struct tm timeinfo = { 0 };
            int retry = 0;
            const int retry_count = 10;
            while(timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
                ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                time(&time_now);
                localtime_r(&time_now, &timeinfo);
            }

            // update 'time_now' variable with current time
            time(&time_now);
        }
        char strftime_buf[64];

        // Set timezone to Eastern Standard Time and print local time
        setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
        tzset();
        localtime_r(&time_now, &timeinfo);

        // update sleep enter time
        struct timeval act_time;
        gettimeofday(&act_time, NULL);
        sleep_enter_time.tv_sec = act_time.tv_sec - sleep_time_ms / 1000;

        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time in New York is: %s", strftime_buf);



        // building request body
        int line_count = count_line_number(&"/spiffs/temperature.txt");
        char* req_body = malloc(line_count * 128);
        strcpy(req_body,  "[");
        
        char line[64];
        char curr_time_buf[64];
        struct tm readout_time_s;

        FILE* f = fopen("/spiffs/temperature.txt", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open temperature file for reading");
            return;  // TODO skip sync this file
        }
        
        int line_idx = 0;
        while (fgets(line, 64, f)) {
            char readout[128];

            char* t_from_reboot = strtok (line, " ");  // in ms
            char* readout_value = strtok (NULL, " ");
            readout_value[strlen(readout_value) - 1] = 0;  // remove \n

            time_t readout_time = sleep_enter_time.tv_sec + atoi(t_from_reboot) / 1000;
            localtime_r(&readout_time, &timeinfo);
            strftime(curr_time_buf, sizeof(curr_time_buf), "%Y-%m-%d %H-%M-%S", &timeinfo);

            if (line_idx != 0) {
                strcat(req_body, ", ");
            }

            snprintf(readout, 128, 
                "{\"timestamp\": \"%s\", \"sensor_type\": \"%s\", \"value\": %s}", 
                curr_time_buf, "TMP", readout_value
            );

            strcat(req_body, readout);\
            line_idx++;
        }
        fclose(f);

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

        
        char* request = malloc(strlen(req_header) + strlen(req_body)); 
        strcpy(request,  &req_header);
        strcat(request,  req_body);

        ESP_LOGI(TAG, "FULL REQUEST: \n%s", request);
        // SYNC data
        //xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
        //http_get_task(request);
    
        ESP_ERROR_CHECK( esp_wifi_stop() );
    }

    // Open light file for reading
    /*
    ESP_LOGI(TAG, "Reading file");
    FILE* f = fopen("/spiffs/temperature.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[256];
    while (fgets(line, 256, f)) {
        printf("%s", line);
    }
    fclose(f);
    */







    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");

    //const int deep_sleep_sec = 10;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", DEEP_SLEEP_DELAY);
    esp_deep_sleep(1000000LL * DEEP_SLEEP_DELAY);
}


int read_adc1_value(int ADC1_CHANNEL)  // TODO check if we need it all every time
{
    esp_adc_cal_characteristics_t characteristics_gt;
    adc1_config_channel_atten(ADC1_CHANNEL, ADC_ATTEN_11db);  
    esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics_gt);

    return adc1_to_voltage(ADC1_CHANNEL, &characteristics_gt);
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
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
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



static void http_get_task(char* REQUEST)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    /* Wait for the callback to set the CONNECTED_BIT in the
        event group.
    */
    //xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
    //                    false, true, portMAX_DELAY);
    //ESP_LOGI(TAG, "Connected to AP");

    while(1) {
        int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
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
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
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
            continue;
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

        break;
    }
}
