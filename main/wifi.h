extern const int CONNECTED_BIT;
extern const int ESP_TOUCH_CONFIG_BIT;
extern const int ESPTOUCH_DONE_BIT;
extern const int WIFI_NOT_SET_BIT;

void obtain_time();
void initialise_wifi(EventGroupHandle_t event_group);
void stop_wifi();