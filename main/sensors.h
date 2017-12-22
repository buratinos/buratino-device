
typedef int (*read_value)();

typedef struct {
    const char *code;
    const char *filepath;
    int read_frequency;
    read_value read;
} sensor_settings_t;


extern sensor_settings_t sensors[5];

void sensor_settings_init();