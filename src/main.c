#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "freertos/queue.h"
#include "esp_timer.h"


// LoRa packets can be up to 255 bits so we can limit our msg size to that
#define MSG_MAX_LEN 256

char message[MSG_MAX_LEN];
size_t msg_len = 0;


QueueHandle_t button_queue;


static const char *TAG = "main";

gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << 35) | (1ULL << 36) | (1ULL << 21),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

gpio_config_t button_conf = {
    .pin_bit_mask = (1ULL << 47),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_ANYEDGE,
};

i2c_master_bus_config_t i2c_conf = {
    .i2c_port = -1,
    .sda_io_num = 17,
    .scl_io_num = 18,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};

const int LCD_CLOCK_SPEED_HZ = 400000;
const int LCD_COMMAND_BIT_WIDTH = 8;
const int LCD_PARAMETER_BIT_WIDTH = 8; 
const int LCD_CONTRL_PHASE_BYTES = 1;
const int LCD_DC_BIT_OFFSET = 6;
esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
esp_lcd_panel_io_i2c_config_t lcd_io_conf = {
    .dev_addr = 0X3C,
    .scl_speed_hz = LCD_CLOCK_SPEED_HZ,
    .lcd_cmd_bits = LCD_COMMAND_BIT_WIDTH,
    .lcd_param_bits = LCD_PARAMETER_BIT_WIDTH,
    .control_phase_bytes = LCD_CONTRL_PHASE_BYTES,
    .dc_bit_offset = LCD_DC_BIT_OFFSET,
};

esp_lcd_panel_handle_t panel_handle = NULL;

esp_lcd_panel_ssd1306_config_t ssd1306_config = {
    .height = 64,
};

esp_lcd_panel_dev_config_t panel_config = {
    .bits_per_pixel = 1,
    .reset_gpio_num = GPIO_NUM_21,
    .vendor_config = &ssd1306_config,
};

const lvgl_port_cfg_t lvgl_conf = ESP_LVGL_PORT_INIT_CONFIG();



void button_task(void *arg) {
    int last_reported = 1;
    int evt;
    const TickType_t SETTLE = pdMS_TO_TICKS(20);
    const TickType_t MSG_GAP = pdMS_TO_TICKS(3000);
    const TickType_t WORD_GAP = pdMS_TO_TICKS(1000);
    const TickType_t MSG_EXTRA = MSG_GAP - WORD_GAP;
    int64_t press_start = 0;

    while (1) {
        BaseType_t got = xQueueReceive(button_queue, &evt, WORD_GAP);
        
        if(got == pdFALSE) {
            if(msg_len > 0 && msg_len < MSG_MAX_LEN - 1) {
                message[msg_len++] = ' ';
                message[msg_len] = '\0';                
            }

            got = xQueueReceive(button_queue, &evt, MSG_EXTRA); 
            if(got == pdFALSE && msg_len > 0) {
                ESP_LOGI(TAG, "%s" , "MessageEnd");
                ESP_LOGI(TAG, "%s" ,message);

                for(int i = 0; i < MSG_MAX_LEN; ++i) {
                    message[i] = '\0';
                }
                msg_len = 0;

                continue;
            }
        }

        while (xQueueReceive(button_queue, &evt, SETTLE) == pdTRUE) {
            // still bouncing, drain and keep waiting
        }



        int level = gpio_get_level(GPIO_NUM_47);
        if (level != last_reported) {
            last_reported = level;


            if(level == 0) {
                press_start = esp_timer_get_time();
                ESP_LOGI(TAG, "%s", "PRESSED");    
            } else {

                int64_t now = esp_timer_get_time();
                int64_t hold_duration = now - press_start;
                ESP_LOGI(TAG, "%s", "RELEASED");

                if(msg_len < MSG_MAX_LEN - 1) {
                    if(hold_duration > 300000) {
                        message[msg_len++] = '_';
                    } else {
                        message[msg_len++] = '.';
                    }
                } else {
                    // for(int i = 0; i < MSG_MAX_LEN; ++i) {
                    //     message[i] = '\0';
                    // }
                    // msg_len = 0;
                    // Force Send Message as we have hit the limit
                }
            }            
            
        }
    }
}


void IRAM_ATTR handle_button_press(void *arg) {
    int ping = 1;
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(button_queue, &ping, &hpw);
    if (hpw) portYIELD_FROM_ISR();
}


void app_main()
{



    gpio_config(&io_conf);
    
    gpio_config(&button_conf);
    gpio_install_isr_service(0);
    button_queue = xQueueCreate(10, sizeof(int));
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    gpio_isr_handler_add(GPIO_NUM_47, &handle_button_press, NULL);



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

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &lcd_io_conf, &lcd_io_handle));
    ESP_LOGI(TAG, "LCD IO DEVICE ALLOCATED!");

    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(lcd_io_handle, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "NEW LCD PANEL CREATED!");

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_conf));


    const lvgl_port_display_cfg_t display_conf = {
        .io_handle = lcd_io_handle,
        .panel_handle = panel_handle,
        .buffer_size = 128*64,
        .hres = 128,
        .vres = 64,
        .monochrome = true,
    };
    
    lv_display_t *disp = lvgl_port_add_disp(&display_conf);

    if (disp == NULL) {
        ESP_LOGE(TAG, "failed to add LVGL display");
        return;
    }
    

    lvgl_port_lock(0);
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "hello");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();


    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // int last = 1;
    // while(1) 
    // {

    //     int now = gpio_get_level(GPIO_NUM_47);
        
    //     if(now != last) {
    //         ESP_LOGI(TAG, "%s", now == 0 ? "PRESSED" : "RELEASED");
    //         last = now;
    //     }

    //     vTaskDelay(pdMS_TO_TICKS(10));

    // }

}