
typedef int (*read_value)();

typedef struct {
    const char *code;
    const char *filepath;
    int read_frequency;
    read_value read;
} sensor_settings_t;


extern sensor_settings_t sensors[4];

int read_temperature_value();
int read_fertility_value();
int read_light_value();
int read_adc1_value(int ADC1_CHANNEL);

void sensor_settings_init();