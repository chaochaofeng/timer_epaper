/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_log.h"
#include "epaper.h"
#include "disp_drv.h"


#define EPD_212_104_HIGHT 212
#define EPD_212_104_WIDTH 104


static u8 epd_212_104_black_buf[EPD_212_104_HIGHT][EPD_212_104_WIDTH / 8];
static u8 epd_212_104_red_buf[EPD_212_104_HIGHT][EPD_212_104_WIDTH / 8];

#if EPD_296x152
#define EPD_296_152_HIGHT 296
#define EPD_296_152_WIDTH 152

static u8 epd_296_152_black_buf[EPD_296_152_HIGHT][EPD_296_152_WIDTH / 8];
static u8 epd_296_152_red_buf[EPD_296_152_HIGHT][EPD_296_152_WIDTH / 8];
#endif

#define mdelay(ms) vTaskDelay(ms / portTICK_RATE_MS)
#define delay_ms(ms) mdelay(ms)

static void Epaper_gpio_Init(void)
{
	gpio_reset_pin(PIN_NUM_DC);
			/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_DC, 0);

	gpio_reset_pin(PIN_NUM_CS);
			/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_CS, 0);

	gpio_reset_pin(PIN_NUM_RST);
		/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_RST, 1);

	gpio_set_direction(PIN_NUM_BUSY, GPIO_MODE_INPUT);
}

static void epaper_reset(void)
{
	gpio_set_level(PIN_NUM_RST, 1);
	delay_ms(200);
	gpio_set_level(PIN_NUM_RST, 0);
	delay_ms(50);
	gpio_set_level(PIN_NUM_RST, 1);
	delay_ms(50);
}

static void epaper_checkbusy(void)
{
	u8 retry_cont = 0;
	while(gpio_get_level(PIN_NUM_BUSY) == 0) {
		delay_ms(100);
		retry_cont++;
		if (retry_cont > 200) {
			printf("check busy timeout\n");
			break;
		}
	}

	printf("busy release\n");
}

static void epaper_dev_init(unsigned int width, unsigned int height)
{
	epaper_reset();

    epaper_checkbusy();

	epaper_spi_send_cmd(0x06); // BOOSTER_SOFT_START
    EPD_2IN13BC_SendData(0x17);
    EPD_2IN13BC_SendData(0x17);
    EPD_2IN13BC_SendData(0x17);
	printf("BOOSTER_SOFT_START\n");
	//delay_ms(1000);

	printf("POWER_ON\n");
    EPD_2IN13BC_SendCommand(0x04); // POWER_ON
	delay_ms(1000);

	//delay_ms(1000);
    EPD_2IN13BC_SendCommand(0x00); // PANEL_SETTING
    EPD_2IN13BC_SendData(0x8F);
	
    EPD_2IN13BC_SendCommand(0x50); // VCOM_AND_DATA_INTERVAL_SETTING
    EPD_2IN13BC_SendData(0xF0);
    EPD_2IN13BC_SendCommand(0x61); // RESOLUTION_SETTING
    EPD_2IN13BC_SendData(width); // width: 104
    EPD_2IN13BC_SendData(height >> 8); // height: 212
    EPD_2IN13BC_SendData(height & 0xFF);
	//delay_ms(1000);
}

static void epaper_turn_ondisplay(void)
{
//	epaper_spi_send_cmd(0x22);
//	epaper_spi_send_data(0xc7);
//	epaper_spi_send_cmd(0x20);
	printf("turn_ondisplay\n");
	epaper_spi_send_cmd(0x12);

	delay_ms(10);
	epaper_checkbusy();
}

void epaper_clear_screen(unsigned int w, unsigned int h)
{
	u16 width = (w % 8 == 0) ? (w / 8):(w / 8 + 1);
	u16 hight = h;

	epaper_spi_send_cmd(0x10);
	for (u16 j = 0; j < hight; j++) {
        for (u16 i = 0; i < width; i++) {
            epaper_spi_send_data(0xFF);
        }
    }
	EPD_2IN13BC_SendCommand(0x92); 

    EPD_2IN13BC_SendCommand(0x13);
    for (u16 j = 0; j < hight; j++) {
        for (u16 i = 0; i < width; i++) {
            EPD_2IN13BC_SendData(0xFF);
        }
    }
    EPD_2IN13BC_SendCommand(0x92); 

	epaper_turn_ondisplay();
}


void EPD_2IN13BC_Display_black_img(const u8* img, u16 width, u16 height)
{
    u16 Width, Height;
    Width = (width % 8 == 0)? (width / 8 ): (width / 8 + 1);
    Height = height;
    
    EPD_2IN13BC_SendCommand(0x10);
#if 1
	spi_send_data(img, Height*Width);
#else
    for (u16 j = 0; j < Height; j++) {
        for (u16 i = 0; i < Width; i++) {
            EPD_2IN13BC_SendData(img[j*Width + i]);
        }
    }
#endif
    EPD_2IN13BC_SendCommand(0x92); 
}

void EPD_2IN13BC_Display_red_img(const u8* img, u16 width, u16 height)
{
    u16 Width, Height;
    Width = (width % 8 == 0)? (width / 8 ): (width / 8 + 1);
    Height = height;


    EPD_2IN13BC_SendCommand(0x13);
#if 1
		spi_send_data(img, Height*Width);
#else
		for (u16 j = 0; j < Height; j++) {
			for (u16 i = 0; i < Width; i++) {
				EPD_2IN13BC_SendData(img[j*Width + i]);
			}
		}
#endif

    EPD_2IN13BC_SendCommand(0x92); 
}

void EPD_2IN13BC_Sleep(void)
{
	printf("epd 2in13 sleep\n");
    EPD_2IN13BC_SendCommand(0x02); // POWER_OFF
    epaper_checkbusy();
    EPD_2IN13BC_SendCommand(0x07); // DEEP_SLEEP
    EPD_2IN13BC_SendData(0xA5); // check code
}

static int epaper_104x212_init(struct disp_dev_st *dev)
{
	struct disp_drv_st *drv = NULL;

	if (!dev) {
		printf("epaper_104x212 init err\n");
		return -1;
	}

	epaper_spi_init(HSPI_HOST);
	Epaper_gpio_Init();

	drv = dev->drv;

	drv->priv_data[EPD_BLACK] 	= dev->buff[EPD_BLACK];
	drv->priv_data[EPD_RED]		= dev->buff[EPD_RED];
	memset(drv->priv_data[EPD_BLACK], 0xff, dev->width * dev->height / 8);
	memset(drv->priv_data[EPD_RED],   0xff, dev->width * dev->height / 8);

	//epaper_dev_init(dev->width, dev->height);
	//epaper_clear_screen(dev->width, dev->height);

	return 0;
}
static void epaper_104x212_uninit(struct disp_dev_st *dev)
{
}

static void epaper_104x212_flush(struct disp_dev_st *dev,
	unsigned int x, unsigned int y,
	unsigned int w, unsigned int h, u8 *fb)
{
	u8 *tmp = fb;
	u8 (*disp_buf)[dev->width / 8];
	struct disp_drv_st *drv = NULL;

	if (!dev) {
		printf("epaper_104x212 flush err\n");
		return;
	}

	drv = dev->drv;

	disp_buf = drv->priv_data[dev->draw_color];

	for (u32 y_index = y; y_index < y + h; y_index++) {
		for (u32 x_index = x / 8; x_index < (x+w) / 8; x_index++) {
			if (y_index > dev->height - 1)
				y_index = dev->height - 1;

			if (x_index > dev->width / 8 -1)
				x_index = dev->width / 8 -1;

			disp_buf[y_index][x_index] = *tmp;
			tmp++;
		}
	}
}
static void epaper_104x212_draw_pixel(struct disp_dev_st *dev, unsigned int x, unsigned int y, u8 color)
{
	u8 (*disp_buf)[dev->width / 8];
	u8 tmp = 0;
	u32 x_index = 0, shift_bit = 0;
	struct disp_drv_st *drv = NULL;

	if (!dev) {
		printf("epaper_104x212 draw_pixel err\n");
		return;
	}
	
	drv = dev->drv;
	disp_buf = drv->priv_data[dev->draw_color];

	x_index = x / 8;
	shift_bit = x % 8;

	tmp = disp_buf[y][x_index];
	if (color)
		tmp |= 1 << (7 - shift_bit);
	else
		tmp &= ~(1 << (7 - shift_bit));

	disp_buf[y][x_index] = tmp;
}
static void epaper_104x212_clear_screen(struct disp_dev_st *dev)
{
	if (!dev) {
		printf("epaper_104x212 clear_screen err\n");
		return;
	}
	epaper_dev_init(dev->width, dev->height);

	epaper_clear_screen(dev->width, dev->height);
}
void epaper_104x212_clear_area_screen(struct disp_dev_st *dev,
		unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
	u8 (*disp_buf)[dev->width / 8];
	struct disp_drv_st *drv = NULL;

	if (!dev) {
		printf("epaper_104x212 clear_area_screen err\n");
		return;
	}

	drv = dev->drv;
	disp_buf = drv->priv_data[dev->draw_color];

	for (u32 y_index = y; y_index < y + h; y_index++) {
		for (u32 x_index = x / 8; x_index < (x+w) / 8; x_index++) {
			disp_buf[y_index][x_index] = 0xFF;
		}
	}
}

static void epaper_104x212_disp_on(struct disp_dev_st *dev)
{
	struct disp_drv_st *drv = NULL;

	if (!dev) {
		printf("epaper_104x212 disp_on err\n");
		return;
	}

	drv = dev->drv;
	epaper_dev_init(dev->width, dev->height);

	EPD_2IN13BC_Display_black_img(drv->priv_data[EPD_BLACK], dev->width, dev->height);
	EPD_2IN13BC_Display_red_img(drv->priv_data[EPD_RED], dev->width, dev->height);

	epaper_turn_ondisplay();
	EPD_2IN13BC_Sleep();
}
static void epaper_104x212_disp_off(struct disp_dev_st *dev)
{
	EPD_2IN13BC_Sleep();
}


struct disp_drv_st epaper_104x212_drv = {
	.name = "epaper_104x212",
	.init			= epaper_104x212_init,
	.uninit			= epaper_104x212_uninit,
	.flush			= epaper_104x212_flush,
	.draw_pixel		= epaper_104x212_draw_pixel,
	.clear_screen	= epaper_104x212_clear_screen,
	.clear_area_screen = epaper_104x212_clear_area_screen,
	.disp_on		= epaper_104x212_disp_on,
	.disp_off		= epaper_104x212_disp_off,
};

struct disp_dev_st epaper_104x212_dev = {
	.name	= "epaper_104x212",
	.width	= EPD_212_104_WIDTH,
	.height	= EPD_212_104_HIGHT,
	.drv	= NULL,
	.buff[EPD_BLACK]	= epd_212_104_black_buf,
	.buff[EPD_RED]	= epd_212_104_red_buf,
};

#if EPD_296x152
struct disp_drv_st epaper_296x152_drv = {
	.name = "epaper_296x152",
	.init			= epaper_104x212_init,
	.uninit			= epaper_104x212_uninit,
	.flush			= epaper_104x212_flush,
	.draw_pixel		= epaper_104x212_draw_pixel,
	.clear_screen	= epaper_104x212_clear_screen,
	.clear_area_screen = epaper_104x212_clear_area_screen,
	.disp_on		= epaper_104x212_disp_on,
	.disp_off		= epaper_104x212_disp_off,
};

struct disp_dev_st epaper_296x152_dev = {
	.name	= "epaper_296x152",
	.width	= EPD_296_152_WIDTH,
	.height	= EPD_296_152_HIGHT,
	.drv	= NULL,
	.buff[EPD_BLACK]	= epd_296_152_black_buf,
	.buff[EPD_RED]	= epd_296_152_red_buf,
};
#endif

