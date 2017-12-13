#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "storage.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include "esp_log.h"


// logging tag
static const char *TAG = "storage";


void get_filepath_for_type(const char* sensor_type, char* filepath);
int count_line_number(char* filepath);
    

void storage_init()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 12,
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
}


void dump_readout(const char* sensor_type, unsigned long at_time, int value)
{
    ESP_LOGI(TAG, "Dumping %s readout at %lu with value: %d", sensor_type, at_time, value);

    char filepath[64];
    get_filepath_for_type(sensor_type, filepath);

    FILE* f = fopen(filepath, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s file for writing", filepath);
        return;
    }

    fprintf(f, "%lu %d\n", at_time, value);
    fclose(f);
}


int get_readouts_count(const char* sensor_type)
{
    char filepath[64];
    get_filepath_for_type(sensor_type, filepath);

    return count_line_number(filepath);
}


void get_readouts(const char* sensor_type, unsigned long* times, int* values)
{
    char filepath[64];
    get_filepath_for_type(sensor_type, filepath);

    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open readout file for reading");
        return;
    }
    
    char line[64];
    int line_idx = 0;

    while (fgets(line, 64, f)) {    
        char* t_from_reboot = strtok (line, " ");  // in ms
        char* readout_value = strtok (NULL, " ");
        readout_value[strlen(readout_value) - 1] = 0;  // remove \n

        times[line_idx] = *t_from_reboot;
        values[line_idx] = *readout_value;
    }
    fclose(f);
}


void flush_readouts(const char* sensor_type)
{
    char filepath[64];
    get_filepath_for_type(sensor_type, filepath);

    struct stat st;  
    if (stat(filepath, &st) == 0) {
        unlink(filepath);
    }
}


void storage_close()
{
    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");    
}


void get_filepath_for_type(const char* sensor_type, char* filepath)
{
    sprintf(filepath, "/spiffs/%s.txt", sensor_type);
}


int count_line_number(char* filepath) {
    int lines = 0;
    int ch;

    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        return 0;
    }

    while( !feof(f) ) {
        ch = fgetc(f);

        if(ch == '\n') {
            lines++;
        }
    }
    fclose(f);

    return lines;
}
