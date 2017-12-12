
typedef int (*read_value)();

typedef struct {
    const char* code;
    const char* filepath;
    read_value read;
} sensor_settings_t;

int read_temperature_value();
int read_fertility_value();
int read_light_value();
int read_adc1_value(int ADC1_CHANNEL);

int get_sensor_number();
void sensor_settings_init(sensor_settings_t* buffer);