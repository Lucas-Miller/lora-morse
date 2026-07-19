#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "main";

gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << 35) | (1ULL << 36) | (1ULL << 21),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

i2c_master_bus_config_t i2c_conf = {
    .i2c_port = -1,
    .sda_io_num = 17,
    .scl_io_num = 18,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};

void app_main()
{
    gpio_config(&io_conf);
    // Set up Vext
    gpio_set_level(GPIO_NUM_36, 0);
    gpio_set_level(GPIO_NUM_21, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(GPIO_NUM_21, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_conf, &bus_handle));

    ESP_ERROR_CHECK(i2c_master_probe(bus_handle, 0x3C, 1000));
    ESP_LOGI(TAG, "SSD1306 found at 0x3C!");


    vTaskDelay(pdMS_TO_TICKS(1000));
    
    while(1) 
    {

        gpio_set_level(GPIO_NUM_35, 1);
        ESP_LOGI(TAG, "ON!");
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(GPIO_NUM_35, 0);
        ESP_LOGI(TAG, "OFF!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}