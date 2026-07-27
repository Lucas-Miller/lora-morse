#include "radio.h"
#include <RadioLib.h>
#include "EspHal.h"
#include "esp_log.h"

static const char *TAG = "main";

const int SCK_GPIO_PIN  = GPIO_NUM_9;
const int MOSI_GPIO_PIN = GPIO_NUM_10;
const int MISO_GPIO_PIN = GPIO_NUM_11;
const int NSS_GPIO_PIN  = GPIO_NUM_8;
const int DIO1_GPIO_PIN = GPIO_NUM_14; 
const int RST_GPIO_PIN  = GPIO_NUM_12; 
const int BUSY_GPIO_PIN = GPIO_NUM_13; 



static EspHal* hal = new EspHal(SCK_GPIO_PIN, MISO_GPIO_PIN, MOSI_GPIO_PIN);
static SX1262 radio = new Module(hal, NSS_GPIO_PIN, DIO1_GPIO_PIN, RST_GPIO_PIN, BUSY_GPIO_PIN);

void radio_init(void) {
    int state = radio.begin(915.0);

    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGI(TAG, "failed, code %d\n", state);
        while(true) {
            hal->delay(1000);
        }
    }
    ESP_LOGI(TAG, "Radio Init Succesful!\n");

}
