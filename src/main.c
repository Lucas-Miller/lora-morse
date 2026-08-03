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
#include "string.h"
#include "radio.h"
#include <stdio.h>

#define OLED_I2C_ADDR 0x3C

const int OLED_I2C_SDA_PIN       = GPIO_NUM_17;
const int OLED_I2C_SCL_PIN       = GPIO_NUM_18;
const int OLED_RESET_PIN         = GPIO_NUM_21; // SSD1306 Reset Pin; Pulse LOW then HIGH to reset
const int ONBOARD_LED_PIN        = GPIO_NUM_35;
const int DISPLAY_VEXT_POWER_PIN = GPIO_NUM_36; // Display Power Rail; LOW = ON 
const int BUTTON_GPIO_PIN        = GPIO_NUM_47; // Active LOW (LOW = Sound)
const int BUZZER_GPIO_PIN        = GPIO_NUM_48;

// LoRa packets can be up to 255 bits so we can limit our msg size to that
#define MSG_MAX_LEN 256

// Constant MS delay amounts
#define DOT_MS         150
#define DASH_MS        (3 * DOT_MS)
#define SYMBOL_GAP_MS  (1 * DOT_MS)
#define LETTER_GAP_MS  (2 * DOT_MS)
#define PRESS_THRESHOLD_MS  (2 * DOT_MS)

// esp_timer_get_time() returns microseconds; the durations above are in ms.
// Convert ms -> us once at the boundary instead of sprinkling "* 1000" around.
#define MS_TO_US(ms)  ((int64_t)(ms) * 1000)

char message[MSG_MAX_LEN];
size_t msg_len = 0;


QueueHandle_t button_queue;
QueueHandle_t message_queue;

volatile bool playing = false;

typedef struct {
    char text[MSG_MAX_LEN];
} morse_message_t;


static const char *TAG = "main";

gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << 35) | (1ULL << 36) | (1ULL << 21) | (1ULL << 48),
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
    .sda_io_num = OLED_I2C_SDA_PIN,
    .scl_io_num = OLED_I2C_SCL_PIN,
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
    .dev_addr = OLED_I2C_ADDR,
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
    .reset_gpio_num = OLED_RESET_PIN,
    .vendor_config = &ssd1306_config,
};

const lvgl_port_cfg_t lvgl_conf = ESP_LVGL_PORT_INIT_CONFIG();

static inline void buzzer_on(void)  { gpio_set_level(BUZZER_GPIO_PIN, 0); }
static inline void buzzer_off(void) { gpio_set_level(BUZZER_GPIO_PIN, 1); }


static void play_beep(int duration_ms) {
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
    vTaskDelay(pdMS_TO_TICKS(SYMBOL_GAP_MS));
}

void play_symbol(char c) {
    if (c == '.') { play_beep(DOT_MS);}
    else if (c == '_') { play_beep(DASH_MS); }
    else if (c == ' ') vTaskDelay(pdMS_TO_TICKS(LETTER_GAP_MS)); // no symbol gap after a letter gap
}

void play_message(morse_message_t *in) {
    size_t message_len = strlen(in->text);
    for(int i = 0; i < message_len; ++i) {
        play_symbol(in->text[i]);
    }

    buzzer_off();

}

void message_consumer_task(void *arg) {
    morse_message_t in;
    while (1) {
        if (xQueueReceive(message_queue, &in, portMAX_DELAY)) {
            ESP_LOGI(TAG, "would send: %s", in.text);
            radio_send(in.text);
        }
    }
}

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
        
        if(got == pdFALSE && !playing) {
            if(msg_len > 0 && msg_len < MSG_MAX_LEN - 1) {
                message[msg_len++] = ' ';
                message[msg_len] = '\0';
            }

            got = xQueueReceive(button_queue, &evt, MSG_EXTRA); 
            if(got == pdFALSE && msg_len > 0) {
                ESP_LOGI(TAG, "%s" , "MessageEnd");
                ESP_LOGI(TAG, "%s" ,message);

                morse_message_t out;

                strncpy(out.text, message, MSG_MAX_LEN);
                out.text[MSG_MAX_LEN - 1] = '\0';

                if(xQueueSend(message_queue, &out, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Message Queue Is Full, Dropping Message...");
                }

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



        int level = gpio_get_level(BUTTON_GPIO_PIN);
        if (level != last_reported && !playing) {
            last_reported = level;


            if(level == 0) {
                press_start = esp_timer_get_time();
                buzzer_on();
                ESP_LOGI(TAG, "%s", "PRESSED");    
            } else {
                buzzer_off();

                int64_t now = esp_timer_get_time();
                int64_t hold_duration = now - press_start;
                ESP_LOGI(TAG, "%s", "RELEASED");

                if(msg_len < MSG_MAX_LEN - 1) {
                    if(hold_duration > MS_TO_US(PRESS_THRESHOLD_MS)) {
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

static lv_obj_t *status_label = NULL;

void set_oled_message(char* msg) {
    if (status_label == NULL) {
        return;   // label not created yet, nothing to update
    }
    lvgl_port_lock(0);
    lv_label_set_text(status_label, msg);
    lvgl_port_unlock();
}

void receive_task(void *arg) {
    char buf[MSG_MAX_LEN];
    morse_message_t *msg_pointer = (morse_message_t *)buf;


    while (1) {
        if (radio_read(buf, sizeof(buf))) {
            char line[MSG_MAX_LEN + 32];
            snprintf(line, sizeof(line), "Received: %s", buf);
            set_oled_message(line);
            ESP_LOGI(TAG, "received: %s", buf);
            playing = true;
            snprintf(line, sizeof(line), "Playing: %s", buf);
            set_oled_message(line);
            play_message(msg_pointer);
            set_oled_message("Waiting...");
            playing = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main()
{
    radio_init();

    radio_start_receive();

    xTaskCreate(receive_task, "receive_task", 4096, NULL, 5, NULL);

    message_queue = xQueueCreate(4, sizeof(morse_message_t));
    xTaskCreate(message_consumer_task, "msg_consumer", 4096, NULL, 5, NULL);

    gpio_config(&io_conf);
    
    gpio_config(&button_conf);
    gpio_set_level(BUZZER_GPIO_PIN, 1);
    gpio_install_isr_service(0);
    button_queue = xQueueCreate(10, sizeof(int));
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    gpio_isr_handler_add(BUTTON_GPIO_PIN, &handle_button_press, NULL);



    // Set up Vext
    gpio_set_level(DISPLAY_VEXT_POWER_PIN, 0);
    gpio_set_level(OLED_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(OLED_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_conf, &bus_handle));

    ESP_ERROR_CHECK(i2c_master_probe(bus_handle, OLED_I2C_ADDR, 1000));
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
    

    // Create the status label once, set_oled_message() will update its text.
    lvgl_port_lock(0);
    status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(status_label, "Waiting...");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();



    vTaskDelay(pdMS_TO_TICKS(1000));

    
    // int last = 1;
    // while(1) 
    // {

    //     int now = gpio_get_level(BUTTON_GPIO_PIN);
        
    //     if(now != last) {
    //         ESP_LOGI(TAG, "%s", now == 0 ? "PRESSED" : "RELEASED");
    //         last = now;
    //     }

    //     vTaskDelay(pdMS_TO_TICKS(10));

    // }

}