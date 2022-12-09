#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp32_u8g2.h"

#define PIN_NUM_RST		4
#define PIN_NUM_MISO	12
#define PIN_NUM_MOSI	13
#define PIN_NUM_CLK		14
#define PIN_NUM_CS		15
#define PIN_NUM_DC		18
#define PIN_NUM_BUSY	5

uint8_t epd_buf[5624];
#define TAG "esp32_u8g2"

static uint8_t esp32_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
	ESP_LOGD(TAG, "gpio_and_delay_cb: Received a msg: %d, arg_int: %d, arg_ptr: %p", msg, arg_int, arg_ptr);

	return 0;
#if 0
	uint8_t retry_cont = 0;

	switch(msg) {
		case U8X8_MSG_GPIO_AND_DELAY_INIT:
			//Epaper_gpio_Init();
			break;
		case U8X8_MSG_GPIO_RESET:
			gpio_set_level(PIN_NUM_RST, arg_int);
			break;
		case U8X8_MSG_GPIO_CS:
			gpio_set_level(PIN_NUM_CS, arg_int);
			//printf("set cs:%d", arg_int);
			break;
		case U8X8_MSG_GPIO_DC:
			gpio_set_level(PIN_NUM_DC, arg_int);
			//printf("set dc:%d", arg_int);
			break;
		case U8X8_MSG_DELAY_MILLI:
			vTaskDelay(arg_int/portTICK_PERIOD_MS);
			break;
		case U8X8_MSG_CAD_WAIT_BUSY:
			while(gpio_get_level(PIN_NUM_BUSY) == arg_int) {
				vTaskDelay(100/portTICK_PERIOD_MS);
				retry_cont++;
				if (retry_cont > 200) {
					printf("check busy timeout\n");
					break;
				}
			}
		default:
			break;
	}

	return 0;
#endif
}


static uint8_t esp32_spi_send_bytes(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
	ESP_LOGD(TAG, "spi_byte_cb: Received a msg: %d, arg_int: %d, arg_ptr: %p", msg, arg_int, arg_ptr);
	return 0;
#if 0
	switch(msg) {
		case U8X8_MSG_BYTE_SET_DC:
			u8x8_gpio_SetDC(u8x8, arg_int);
			break;
		case U8X8_MSG_BYTE_INIT:
			spi_init(HSPI_HOST);
			break;
		case U8X8_MSG_CAD_SEND_CMD:
			//u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_GPIO_DC, 0, NULL);
			//esp32_spi_send_byte();
			break;
		case U8X8_MSG_BYTE_SEND:;//=U8X8_MSG_CAD_SEND_DATA
			//u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_GPIO_DC, 1, NULL);

		    //trans.cmd = 1;
		    int i = 0;
			uint8_t *tmp = (uint8_t *)arg_ptr;
			for (i = 0; i < arg_int;i++)
			{
				ESP_LOGI(TAG, "0x%x ", tmp[i]);
				esp32_spi_send_data(tmp[i]);
			}

		    //ret=spi_device_queue_trans(spi, &trans[5], portMAX_DELAY);
			break;
		case U8X8_MSG_BYTE_START_TRANSFER:
			u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);  
			break;
		case U8X8_MSG_BYTE_END_TRANSFER:
			u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);  
			break;
		default:
			break;
	}

	return 0;
#endif
}

void u8g2_Setup_epd_2in66_296x152_f(u8g2_t *u8g2, const u8g2_cb_t *rotation, u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb)
{
  uint8_t tile_buf_height = 19;
  uint8_t *buf;
  u8g2_SetupDisplay(u8g2, u8x8_d_epd_2in66_296x152, u8x8_cad_011, byte_cb, gpio_and_delay_cb);
  buf = epd_buf;
  memset(buf, 0xff, 5624);
  u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb, rotation);
}

void esp32_u8g2_setup(u8g2_t *u8g2)
{
	u8g2_Setup_epd_2in66_296x152_f(u8g2, U8G2_R0, esp32_spi_send_bytes, esp32_gpio_and_delay_cb);
}
