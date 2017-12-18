#include <stdio.h>
#include "sensors.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ds18b20.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/i2c.h"


#define V_REF 1100

#define ADC1_TEMP_CHANNEL 14                // GPIO 14
#define ADC1_FERT_CHANNEL (ADC1_CHANNEL_6)  // GPIO 34, A2 Feather
#define ADC1_LIGHT_CHANNEL (ADC1_CHANNEL_0) // GPIO 36, A4 Feather

#define DATA_LENGTH                        512              /*!<Data buffer length for test buffer*/
//#define RW_TEST_LENGTH                     129              /*!<Data length for r/w test, any value from 0-DATA_LENGTH*/
//#define DELAY_TIME_BETWEEN_ITEMS_MS        1234             /*!< delay time between different test items */

#define I2C_EXAMPLE_MASTER_SCL_IO          22               /*!< gpio number for I2C master clock */
#define I2C_EXAMPLE_MASTER_SDA_IO          23               /*!< gpio number for I2C master data  */
#define I2C_EXAMPLE_MASTER_NUM             I2C_NUM_1        /*!< I2C port number for master dev */
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE  0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE  0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_FREQ_HZ         100000           /*!< I2C master clock frequency */

#define SOIL_SENSOR_ADDR                   0x20             /*!< slave address for BH1750 sensor */
#define SOIL_CMD_START                     0x00             /*!< Command to set measure mode */
#define WRITE_BIT                          I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                           I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                       0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                      0x0              /*!< I2C master will not check ack from slave */
#define ACK_VAL                            0x0              /*!< I2C ack value */
#define NACK_VAL                           0x1              /*!< I2C nack value */

#define FREQ_TEMPERATURE 1      // read out temperatute every X reboots
#define FREQ_LIGHT 1            // read out light every X reboots
#define FREQ_FERTILITY 1        // read out resistive soil every X reboots
#define FREQ_SOIL 1             // read out capacitive soil every X reboots

// logging tag
static const char *TAG = "sensors";

sensor_settings_t sensors[4];


int read_temperature_value()
{
    ESP_LOGI(TAG, "Reading temperature sensor");
    ds18b20_init(ADC1_TEMP_CHANNEL);

    float temp = ds18b20_get_temp() * 100;  // 0.2f to int

    return (int)temp;
}


int read_fertility_value()
{
    ESP_LOGI(TAG, "Reading fertility sensor");
    return read_adc1_value(ADC1_FERT_CHANNEL);
}


int read_light_value()
{
    ESP_LOGI(TAG, "Reading light sensor");
    return read_adc1_value(ADC1_LIGHT_CHANNEL);
}



int read_soil_value()
{
    ESP_LOGI(TAG, "Reading soil sensor");

    uint8_t sensor_data_h, sensor_data_l;
    
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, SOIL_SENSOR_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, SOIL_CMD_START, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_EXAMPLE_MASTER_NUM, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return -1;  // TODO: return error, put data into *int
    }
    vTaskDelay(30 / portTICK_RATE_MS);
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, SOIL_SENSOR_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, &sensor_data_h, ACK_VAL);
    i2c_master_read_byte(cmd, &sensor_data_l, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_EXAMPLE_MASTER_NUM, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_ERR_TIMEOUT) {
        printf("I2C timeout\n");
    } else if (ret == ESP_OK) {
        float value = (sensor_data_h << 8 | sensor_data_l) / 1.2;
        return (int)value;
    } else {
        printf("No ack, sensor not connected...skip...\n");
    }

    return -1;
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
    return 4;
}


void sensor_settings_init()
{
    ESP_LOGI(TAG, "Setting up analog channels\n");

    adc1_config_width(ADC_WIDTH_BIT_12);

    ESP_LOGI(TAG, "Setting up I2C channels\n");

    int i2c_master_port = I2C_EXAMPLE_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_EXAMPLE_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_EXAMPLE_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_EXAMPLE_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_EXAMPLE_MASTER_RX_BUF_DISABLE,
                       I2C_EXAMPLE_MASTER_TX_BUF_DISABLE, 0);

    sensor_settings_t temp_sensor = {
        .code = "TMP",
        .filepath = "/spiffs/temperature.txt",
        .read_frequency = FREQ_TEMPERATURE,
        .read = read_temperature_value
    };

    sensor_settings_t fert_sensor = {
        .code = "FER",
        .filepath = "/spiffs/fertility.txt",
        .read_frequency = FREQ_FERTILITY,
        .read = read_fertility_value
    };

    sensor_settings_t light_sensor = {
        .code = "LUM",
        .filepath = "/spiffs/light.txt",
        .read_frequency = FREQ_LIGHT,
        .read = read_light_value
    };

    sensor_settings_t soil_sensor = {
        .code = "MTR",
        .filepath = "/spiffs/soil.txt",
        .read_frequency = FREQ_SOIL,
        .read = read_soil_value
    };
    
    sensors[0] = temp_sensor;
    sensors[1] = fert_sensor;
    sensors[2] = light_sensor;
    sensors[3] = soil_sensor;    
}
