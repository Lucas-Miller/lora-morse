#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static const char *TAG = "main";

gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << 35),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

void app_main()
{
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_35, 1);
    
    while(1) 
    {


        ESP_LOGI(TAG, "Hello World!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}