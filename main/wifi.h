extern const int CONNECTED_BIT;
extern const int ESP_TOUCH_CONFIG_BIT;
extern const int ESPTOUCH_DONE_BIT;

void obtain_time();
void initialise_wifi(EventGroupHandle_t event_group);
void stop_wifi();