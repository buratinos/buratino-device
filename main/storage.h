
void storage_init();

void dump_readout(const char* sensor_type, unsigned long time, int value);

int get_readouts_count(const char* sensor_type);

void get_readouts(const char* sensor_type, unsigned long* times, int* values);

void flush_readouts(const char* sensor_type);

void storage_close();