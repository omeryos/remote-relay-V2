#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/gpio_types.h"
#include "hal/uart_types.h"
#include "soc/gpio_num.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#define UART_PORT UART_NUM_1
#define BUF_SIZE 512
#define TXD_PIN 17
#define RXD_PIN 16
#define RELAY_PIN GPIO_NUM_5

static const char *TAG = "SMS";
static uint8_t data[BUF_SIZE];
static const char * const AT_CMDS[] = {
	"ATE0\r\n",
    "AT+CMGF=1\r\n",
    "AT+CNMI=2,2,0,0,0\r\n"
};

int isOk = 0;


esp_err_t process_sms(const char *msg,const char *pass)
{
	
    if (strstr(msg, pass)) {
        ESP_LOGI(TAG, "pass ok");
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

void set_gpio_pin_remote(gpio_num_t pin){
	gpio_set_level(pin, 1);
	vTaskDelay(pdMS_TO_TICKS(300));
	gpio_set_level(pin, 0);
}


void app_main(void)
{
	// GPIO
    gpio_config_t io = {
    .pin_bit_mask = 1ULL << RELAY_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
};

    gpio_config(&io);
    gpio_set_level(RELAY_PIN, 0);
	
	
	//UART
	vTaskDelay(pdMS_TO_TICKS(5000)); 			//give the module time to establish connection
	uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		 .flow_ctrl =  UART_HW_FLOWCTRL_DISABLE
	};
	
	uart_driver_install(UART_PORT,
	  BUF_SIZE * 2,
	  BUF_SIZE * 2,
	  0,
	  NULL,
	  0);
	
	uart_param_config(UART_PORT,&uart_config);
	
	uart_set_pin(UART_PORT,
	 TXD_PIN,
	  RXD_PIN,
	   UART_PIN_NO_CHANGE,
	   UART_PIN_NO_CHANGE);
	
	size_t num_cmds = sizeof(AT_CMDS)/sizeof(AT_CMDS[0]);
	for(int i = 0; i < num_cmds ; i++){
		isOk = uart_write_bytes(UART_PORT, AT_CMDS[i], strlen(AT_CMDS[i]));
		if(isOk == -1 || isOk != strlen(AT_CMDS[i])) {
			ESP_LOGI(TAG, "transmission of AT command %d to set up module failed! isOk param: %d",i, isOk);
			esp_restart(); 
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
	
 	uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(500));
	uart_flush_input(UART_PORT);  
	
	while(1){
		int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
        if (len > 0){
			data[len] = 0; 
            char *line = strtok((char*)data, "\r\n");
            while(line != NULL){
				if(process_sms(line,"1960s") == ESP_OK){
				set_gpio_pin_remote(RELAY_PIN);
				}
				line = strtok(NULL, "\r\n");
			}   
            
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
	
}
