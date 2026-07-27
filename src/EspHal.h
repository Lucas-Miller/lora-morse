#ifndef ESP_HAL_H
#define ESP_HAL_H

// include RadioLib
#include <RadioLib.h>

// include all the dependencies
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"

// define Arduino-style macros
#define LOW                         (0x0)
#define HIGH                        (0x1)
#define INPUT                       (0x01)
#define OUTPUT                      (0x03)
#define RISING                      (0x01)
#define FALLING                     (0x02)
#define NOP()                       asm volatile ("nop")

// ESP-IDF hardware abstraction layer for RadioLib.
// SPI is implemented on the portable spi_master driver so it works on the ESP32-S3.
class EspHal : public RadioLibHal {
  public:
    // constructor - stores the SPI pins and initializes the base HAL
    EspHal(int8_t sck, int8_t miso, int8_t mosi)
      : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
        spiSCK(sck), spiMISO(miso), spiMOSI(mosi) {
    }

    void init() override {
      spiBegin();
    }

    void term() override {
      spiEnd();
    }

    void pinMode(uint32_t pin, uint32_t mode) override {
      if(pin == RADIOLIB_NC) {
        return;
      }
      gpio_config_t conf = {};
      conf.pin_bit_mask = (1ULL << pin);
      conf.mode         = (gpio_mode_t)mode;
      conf.pull_up_en   = GPIO_PULLUP_DISABLE;
      conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
      conf.intr_type    = GPIO_INTR_DISABLE;
      gpio_config(&conf);
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
      if(pin == RADIOLIB_NC) {
        return;
      }
      gpio_set_level((gpio_num_t)pin, value);
    }

    uint32_t digitalRead(uint32_t pin) override {
      if(pin == RADIOLIB_NC) {
        return(0);
      }
      return(gpio_get_level((gpio_num_t)pin));
    }

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
      if(interruptNum == RADIOLIB_NC) {
        return;
      }
      gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
      gpio_set_intr_type((gpio_num_t)interruptNum, (gpio_int_type_t)(mode & 0x7));
      gpio_isr_handler_add((gpio_num_t)interruptNum, (void (*)(void*))interruptCb, NULL);
    }

    void detachInterrupt(uint32_t interruptNum) override {
      if(interruptNum == RADIOLIB_NC) {
        return;
      }
      gpio_isr_handler_remove((gpio_num_t)interruptNum);
      gpio_wakeup_disable((gpio_num_t)interruptNum);
      gpio_set_intr_type((gpio_num_t)interruptNum, GPIO_INTR_DISABLE);
    }

    void delay(unsigned long ms) override {
      vTaskDelay(ms / portTICK_PERIOD_MS);
    }

    void delayMicroseconds(unsigned long us) override {
      uint64_t m = (uint64_t)esp_timer_get_time();
      if(us) {
        uint64_t e = (m + us);
        if(m > e) { // overflow
          while((uint64_t)esp_timer_get_time() > e) {
            NOP();
          }
        }
        while((uint64_t)esp_timer_get_time() < e) {
          NOP();
        }
      }
    }

    unsigned long millis() override {
      return((unsigned long)(esp_timer_get_time() / 1000ULL));
    }

    unsigned long micros() override {
      return((unsigned long)(esp_timer_get_time()));
    }

    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override {
      if(pin == RADIOLIB_NC) {
        return(0);
      }
      this->pinMode(pin, INPUT);
      uint32_t start = this->micros();
      uint32_t curtick = this->micros();
      while(this->digitalRead(pin) == state) {
        if((this->micros() - curtick) > timeout) {
          return(0);
        }
      }
      return(this->micros() - start);
    }

    // ---- SPI, rewritten on the portable spi_master driver (S3-compatible) ----

    void spiBegin() {
      spi_bus_config_t buscfg = {};
      buscfg.mosi_io_num   = this->spiMOSI;
      buscfg.miso_io_num   = this->spiMISO;
      buscfg.sclk_io_num   = this->spiSCK;
      buscfg.quadwp_io_num = -1;
      buscfg.quadhd_io_num = -1;
      spi_bus_initialize(SPI_HOST_ID, &buscfg, SPI_DMA_DISABLED);

      spi_device_interface_config_t devcfg = {};
      devcfg.mode           = 0;          // SPI mode 0 - what the SX1262 uses
      devcfg.clock_speed_hz = 2000000;    // 2 MHz
      devcfg.spics_io_num   = -1;         // no hardware CS; RadioLib drives CS itself
      devcfg.queue_size     = 1;
      spi_bus_add_device(SPI_HOST_ID, &devcfg, &this->spiHandle);
    }

    void spiBeginTransaction() {
      // nothing needed - mode/clock are set on the device, CS is handled by RadioLib
    }

    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
      if(len == 0) {
        return;
      }
      spi_transaction_t t = {};
      t.length    = len * 8;   // length is in BITS
      t.tx_buffer = out;
      t.rx_buffer = in;
      spi_device_polling_transmit(this->spiHandle, &t);
    }

    void spiEndTransaction() {
      // nothing needed
    }

    void spiEnd() {
      spi_bus_remove_device(this->spiHandle);
      spi_bus_free(SPI_HOST_ID);
    }

  private:
    int8_t spiSCK;
    int8_t spiMISO;
    int8_t spiMOSI;
    spi_device_handle_t spiHandle = nullptr;
    static constexpr spi_host_device_t SPI_HOST_ID = SPI2_HOST;  // S3's general-purpose SPI
};

#endif
