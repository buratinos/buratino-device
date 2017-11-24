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

#include <stdlib.h>
#include <string.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#define V_REF 1100
#define TEMP_GPIO 14
#define ADC1_SOIL_CHANNEL (ADC1_CHANNEL_6) //GPIO 34, A2 Feather
#define ADC1_LIGHT_CHANNEL (ADC1_CHANNEL_0) //GPIO 36, A4 Feather

uint32_t voltage;


void app_main()
{
    printf("Hello world!\n");

    ds18b20_init(TEMP_GPIO);

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d\n", chip_info.revision);

    while (1) {
        printf("Temperature: %0.1f\n", ds18b20_get_temp());

        esp_adc_cal_characteristics_t characteristics;
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(ADC1_LIGHT_CHANNEL, ADC_ATTEN_11db);
        esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics);

        printf("Light: %d\n", adc1_to_voltage(ADC1_LIGHT_CHANNEL, &characteristics));

        esp_adc_cal_characteristics_t characteristics2;
        adc1_config_channel_atten(ADC1_SOIL_CHANNEL, ADC_ATTEN_11db);
        esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics2);

        printf("Soil: %d\n", adc1_to_voltage(ADC1_SOIL_CHANNEL, &characteristics2));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /*
    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    for (int i = 2; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
    */
}
