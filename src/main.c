#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "main";

void app_main()
{
    while(1) 
    {


        ESP_LOGI(TAG, "Hello World!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}