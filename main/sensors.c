#include <stdio.h>
#include "sensors.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ds18b20.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"
#include "driver/adc.h"


#define V_REF 1100

#define ADC1_TEMP_CHANNEL 14                // GPIO 14
#define ADC1_FERT_CHANNEL (ADC1_CHANNEL_6)  // GPIO 34, A2 Feather
#define ADC1_LIGHT_CHANNEL (ADC1_CHANNEL_0) // GPIO 36, A4 Feather

// logging tag
static const char *TAG = "sensors";


int read_temperature_value()
{
    ESP_LOGI(TAG, "Reading temperature sensor\n");
    ds18b20_init(ADC1_TEMP_CHANNEL);

    float temp = ds18b20_get_temp() * 100;  // 0.2f to int

    return (int)temp;
}


int read_fertility_value()
{
    ESP_LOGI(TAG, "Reading fertility sensor\n");
    return read_adc1_value(ADC1_FERT_CHANNEL);
}


int read_light_value()
{
    ESP_LOGI(TAG, "Reading light sensor\n");
    return read_adc1_value(ADC1_LIGHT_CHANNEL);
}


int read_adc1_value(int ADC1_CHANNEL)  // TODO check if we need it all every time
{
    esp_adc_cal_characteristics_t characteristics_gt;
    adc1_config_channel_atten(ADC1_CHANNEL, ADC_ATTEN_11db);  
    esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, &characteristics_gt);

    return adc1_to_voltage(ADC1_CHANNEL, &characteristics_gt);
}


int get_sensor_number()
{
    return 3;
}


void sensor_settings_init(sensor_settings_t* buffer)
{
    ESP_LOGI(TAG, "Setting up analog channels\n");
    adc1_config_width(ADC_WIDTH_BIT_12);

    sensor_settings_t temp_sensor = {
        .code = "TMP",
        .filepath = "/spiffs/temperature.txt",
        .read = read_temperature_value
    };

    sensor_settings_t fert_sensor = {
        .code = "FER",
        .filepath = "/spiffs/fertility.txt",
        .read = read_fertility_value
    };

    sensor_settings_t light_sensor = {
        .code = "LUM",
        .filepath = "/spiffs/light.txt",
        .read = read_light_value
    };

    buffer[0] = temp_sensor;
    buffer[1] = fert_sensor;
    buffer[2] = light_sensor;
}
