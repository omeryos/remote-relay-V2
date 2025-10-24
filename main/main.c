#include "button_gpio.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "hal/uart_types.h"
#include "iot_button.h"
#include "soc/gpio_num.h"
#include "ssd1306.h" // from nopnop2002 library
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_PORT UART_NUM_1
#define BUF_SIZE 512
#define TXD_PIN 17
#define RXD_PIN 16
#define RELAY_PIN GPIO_NUM_18

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define OLED_ADDR 0x3C

static SSD1306_t dev;
static const char *TAG = "SMS";
static uint8_t data[BUF_SIZE];
static const char *const AT_CMDS[] = {"ATE0\r\n", "AT+CMGF=1\r\n",
                                      "AT+CNMI=2,2,0,0,0\r\n"};

int isOk = 0;

void display_init(void) {
  i2c_master_init(&dev, I2C_SDA_PIN, I2C_SCL_PIN, -1);
  ssd1306_init(&dev, 128, 64); // 128x64 OLED
  ssd1306_clear_screen(&dev, false);
}

void showMessage(const char *text, int ms, bool isConst) {
  const int rows = dev._height / 8; // 128x64 -> 8 rows, 128x32 -> 4 rows
  ssd1306_clear_screen(&dev, false);

  const char *p = text;
  for (int row = 0; row < rows && *p; ++row) {
    // ~21 visible chars with 6x8 font across 128px width; adjust if you use a
    // different font
    char line[22];
    int n = 0;

    // copy one line up to newline or max chars
    while (p[n] && p[n] != '\n' && n < (int)sizeof(line) - 1)
      n++;
    memcpy(line, p, n);
    line[n] = '\0';

    // draw this row (row index 0..rows-1)
    ssd1306_display_text(&dev, row, line, n, false);

    // advance to next source line (skip the newline if present)
    p += n;
    if (*p == '\n')
      p++;
  }

  vTaskDelay(pdMS_TO_TICKS(ms));
  if (!isConst)
    ssd1306_clear_screen(&dev, false);
}

// printf-style helper that uses the multi-line showMessage()
void showMessagef(int ms, const char *fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  showMessage(buf, ms, false);
}

esp_err_t process_sms(const char *msg, const char *pass) {

  if (strstr(msg, pass)) {
    return ESP_OK;
  } else {
    return ESP_ERR_INVALID_ARG;
  }
}

void set_gpio_pin_remote(gpio_num_t pin) {
  gpio_set_level(pin, 1);
  vTaskDelay(pdMS_TO_TICKS(300));
  gpio_set_level(pin, 0);
}
static void button_single_click_cb(void *arg, void *usr_data) {
  set_gpio_pin_remote(RELAY_PIN);
  showMessage("Button Pressed!", 500, false);
  showMessage("Listening for \nIncoming SMS", 50, true);
}

void app_main(void) {
  display_init();

  // GPIO
  gpio_config_t io = {.pin_bit_mask = 1ULL << RELAY_PIN,
                      .mode = GPIO_MODE_OUTPUT,
                      .pull_up_en = GPIO_PULLUP_DISABLE,
                      .pull_down_en = GPIO_PULLDOWN_DISABLE,
                      .intr_type = GPIO_INTR_DISABLE};

  esp_err_t status;
  status = gpio_config(&io);
  ESP_LOGI(TAG, "gpio config status: %d", status);
  gpio_set_level(RELAY_PIN, 0);

  // BUTTON
  //  create gpio button
  const button_config_t btn_cfg = {0};
  const button_gpio_config_t btn_gpio_cfg = {
      .gpio_num = 25,
      .active_level = 1,
      .disable_pull = false,
  };
  button_handle_t gpio_btn = NULL;
  iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &gpio_btn);
  if (NULL == gpio_btn) {
    ESP_LOGI(TAG, "Button create failed");
  }
  iot_button_register_cb(gpio_btn, BUTTON_SINGLE_CLICK, NULL,
                         button_single_click_cb, NULL);

  // UART
  vTaskDelay(
      pdMS_TO_TICKS(5000)); // give the module time to establish connection
  uart_config_t uart_config = {.baud_rate = 115200,
                               .data_bits = UART_DATA_8_BITS,
                               .parity = UART_PARITY_DISABLE,
                               .stop_bits = UART_STOP_BITS_1,
                               .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

  uart_driver_install(UART_PORT, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);

  status = uart_param_config(UART_PORT, &uart_config);
  ESP_LOGI(TAG, "status of uart_param_config:%d ", status);

  status = uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE,
                        UART_PIN_NO_CHANGE);

  ESP_LOGI(TAG, "status of uart_set_pin: %d ", status);
  size_t num_cmds = sizeof(AT_CMDS) / sizeof(AT_CMDS[0]);
  for (int i = 0; i < num_cmds; i++) {
    isOk = uart_write_bytes(UART_PORT, AT_CMDS[i], strlen(AT_CMDS[i]));
    showMessagef(2000, "status of \nuart_write_bytes \nfor \nAT CMD %d: %s ",
                 i + 1, isOk == strlen(AT_CMDS[i]) ? "Ok" : "ERR");

    if (isOk == -1 || isOk != strlen(AT_CMDS[i])) {
      showMessage("AT ERR", 1500, false);
      esp_restart();
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(500));
  uart_flush_input(UART_PORT);

  showMessage("Setup complete!\nListening for \nincoming SMS", 2500, true);

  while (1) {
    int len =
        uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
    int stat = 0;
    if (len > 0) {
      ESP_LOGI(TAG, "SMS RECIEVED!");
      showMessagef(1200, "SMS RECIEVED!\nLength:%d", len);
      data[len] = 0;
      char *line = strtok((char *)data, "\r\n");
      while (line != NULL) {
        if ((stat = process_sms(line, "1960s") == ESP_OK)) {
          set_gpio_pin_remote(RELAY_PIN);
          break;
        }
        showMessagef(1000, "SMS pass RCV\n result code:\n%d", stat);
        ESP_LOGI(TAG, "SMS pass RCV\n result code:\n%d", stat);
        line = strtok(NULL, "\r\n");
      }
      showMessage("Listening for \nIncoming SMS", 50, true);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
