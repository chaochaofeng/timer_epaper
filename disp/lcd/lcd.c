/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "./demos/widgets/lv_demo_widgets.h"
#include "lvgl.h"
#include "lcd_init.h"
#include "pic.h"
#include "xiong_msb.h"
#include "tutu.h"
#include "pretty.h"

/* Can use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO 2
#define mdelay(ms) vTaskDelay(ms / portTICK_RATE_MS)
#define delay_ms(ms) mdelay(ms)

static void myguiTask(void *pvParameter);

void *lcd_queue = NULL;
spi_device_handle_t spi = {0};




void LCD_GPIO_Init(void)
{
	gpio_reset_pin(PIN_NUM_DC);
			/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_DC, 1);
	
	gpio_reset_pin(PIN_NUM_RST);
		/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_RST, 1);

	gpio_reset_pin(PIN_NUM_BCKL);
		/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_BCKL, 0);
}

void lcd_send_data(spi_device_handle_t spi, uint8_t data)
{
	esp_err_t ret;
	spi_transaction_t t;

	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length = 8;					  //Command is 8 bits
	t.tx_buffer = &data; 			  //The data is the cmd itself

	ret = spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

void lcd_send_data16(spi_device_handle_t spi, uint16_t data)
{
	esp_err_t ret;
	spi_transaction_t t;

	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length = 16;					  //Command is 8 bits
	t.tx_buffer = &data;			  //The data is the cmd itself

	ret = spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

void lcd_send_cmd(spi_device_handle_t spi, uint8_t cmd)
{
	esp_err_t ret;
	spi_transaction_t t;

	LCD_DC_Clr();

	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length = 8;					  //Command is 8 bits
	t.tx_buffer = &cmd; 			  //The data is the cmd itself

	ret = spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.

	LCD_DC_Set();
}

void lcd_address_set(u16 x1,u16 y1,u16 x2,u16 y2)
{
	if(USE_HORIZONTAL==0)
	{
		lcd_send_cmd(spi, 0x2a);//列地址设置
		lcd_send_data(spi, (x1 & 0xff00)>>8);
		lcd_send_data(spi, x1 & 0xff);
		lcd_send_data(spi, (x2 & 0xff00)>>8);
		lcd_send_data(spi, x2 & 0xff);
		lcd_send_cmd(spi, 0x2b);//行地址设置
		lcd_send_data(spi, (y1 & 0xff00)>>8);
		lcd_send_data(spi, y1 & 0xff);
		lcd_send_data(spi, (y2 & 0xff00)>>8);
		lcd_send_data(spi, y2 & 0xff);
		//lcd_send_data(spi, y1);
		//lcd_send_data(spi, y2);
		lcd_send_cmd(spi, 0x2c);//储存器写
	}
	else if(USE_HORIZONTAL==1)
	{
		lcd_send_cmd(spi, 0x2a);//列地址设置
		lcd_send_data(spi, x1);
		lcd_send_data(spi, x2);
		lcd_send_cmd(spi, 0x2b);//行地址设置
		lcd_send_data(spi, y1+80);
		lcd_send_data(spi, y2+80);
		lcd_send_cmd(spi, 0x2c);//储存器写
	}
	else if(USE_HORIZONTAL==2)
	{
		lcd_send_cmd(spi, 0x2a);//列地址设置
		lcd_send_data(spi, x1);
		lcd_send_data(spi, x2);
		lcd_send_cmd(spi, 0x2b);//行地址设置
		lcd_send_data(spi, y1);
		lcd_send_data(spi, y2);
		lcd_send_cmd(spi, 0x2c);//储存器写
	}
	else
	{
		lcd_send_cmd(spi, 0x2a);//列地址设置
		lcd_send_data(spi, x1+80);
		lcd_send_data(spi, x2+80);
		lcd_send_cmd(spi, 0x2b);//行地址设置
		lcd_send_data(spi, y1);
		lcd_send_data(spi, y2);
		lcd_send_cmd(spi, 0x2c);//储存器写
	}	
}

void lcd_send_data_cnt(spi_device_handle_t spi, u8 *buf, u32 cnt)
{
	esp_err_t ret;
	spi_transaction_t t;

	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length = cnt * 8;					  //Command is 8 bits
	t.tx_buffer = &buf; 			  //The data is the cmd itself
	t.flags = 0;

	ret = spi_device_queue_trans(spi, &t, portMAX_DELAY);

	//ret = spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

void lcd_fill_range(u16 xsta,u16 ysta,u16 xend,u16 yend,u16 color)
{          
	u16 i,j; 
	lcd_address_set(xsta,ysta,xend-1,yend-1);//设置显示范围
	for(i=ysta;i<yend;i++)
	{													   	 	
		for(j=xsta;j<xend;j++)
		{
			lcd_send_data16(spi, color);
		}
	} 					  	    
}

void lcd_draw_pic(u16 xsta,u16 ysta,u16 xend,u16 yend,u8 *buf)
{
	u16 i,j; 
	lcd_address_set(xsta,ysta,xend-1,yend-1);//设置显示范围
	lcd_send_data_cnt(spi, buf, 100);
}


void LCD_ShowPicture(u16 x,u16 y,u16 length,u16 width,const u8 pic[])
{
	u16 i,j;
	u32 k=0;
	lcd_address_set(x,y,x+length-1,y+width-1);
	for(i=0;i<length;i++)
	{
		for(j=0;j<width;j++)
		{
			lcd_send_data(spi, pic[k*2]);
			lcd_send_data(spi, pic[k*2+1]);
			k++;
		}
	}			
}

#define PARALLEL_LINES 16

static void send_lines(spi_device_handle_t spi, uint8_t *linedata, uint16_t bit_len)
{
    esp_err_t ret;

    static spi_transaction_t trans;

    trans.tx_buffer = linedata;        //finally send the line data
    trans.length = bit_len ;          //Data length, in bits
    trans.flags = 0; //undo SPI_TRANS_USE_TXDATA flag
    trans.rxlength = 0;
    //trans.cmd = 1;

    //ret=spi_device_queue_trans(spi, &trans[5], portMAX_DELAY);
    ret = spi_device_polling_transmit(spi, &trans);
    assert(ret==ESP_OK);

}

static void lcd_flush(spi_device_handle_t spi, u16 xsta,u16 ysta,u16 width,u16 height,uint8_t *linedata)
{
	uint16_t bit_len = 0;
	static uint16_t max_send_bits = 240 * 16 * 2;
	static uint16_t max_send_bytes = 240 * 16 * 2 / 8;

	uint8_t *tmp = linedata;
	uint32_t remain_size = width * height * 2;

    lcd_address_set(xsta, ysta, xsta + width - 1, ysta + height - 1);

	while (remain_size / max_send_bytes) {
		send_lines(spi, tmp, max_send_bits);
		remain_size -= max_send_bytes;
		tmp += max_send_bytes;
	}

	if (remain_size)
		send_lines(spi, tmp, remain_size * 8);
}

void lcd_spi_init(spi_host_device_t host_id)
{
	esp_err_t ret;

	//Initialize the SPI bus
	ret = spi_bus_initialize(host_id, &buscfg, 2);
	ESP_ERROR_CHECK(ret);
	//Attach the LCD to the SPI bus
	ret = spi_bus_add_device(host_id, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);

}

void lcd_init(void)
{
	LCD_GPIO_Init();//初始化GPIO

	LCD_RES_Clr();//复位
	delay_ms(100);
	LCD_RES_Set();
	delay_ms(100);

	LCD_BLK_Set();//打开背光
	//************* Start Initial Sequence **********//
	lcd_send_cmd(spi, 0x11); //Sleep out 
	delay_ms(120);              //Delay 120ms 
	//************* Start Initial Sequence **********//
	lcd_send_cmd(spi, 0x36);
	if(USE_HORIZONTAL==0)      lcd_send_data(spi, 0x00);
	else if(USE_HORIZONTAL==1) lcd_send_data(spi, 0xC0);
	else if(USE_HORIZONTAL==2) lcd_send_data(spi, 0x70);
	else                       lcd_send_data(spi, 0xA0);

	lcd_send_cmd(spi, 0x3A); 
	lcd_send_data(spi, 0x55);                          //65k, 565

	lcd_send_cmd(spi, 0xB2);
	lcd_send_data(spi, 0x0C);
	lcd_send_data(spi, 0x0C);
	lcd_send_data(spi, 0x00);
	lcd_send_data(spi, 0x33);
	lcd_send_data(spi, 0x33); 

	lcd_send_cmd(spi, 0xB7); 
	lcd_send_data(spi, 0x35);  

	lcd_send_cmd(spi, 0xBB);
	lcd_send_data(spi, 0x19);

	lcd_send_cmd(spi, 0xC0);
	lcd_send_data(spi, 0x2C);

	lcd_send_cmd(spi, 0xC2);
	lcd_send_data(spi, 0x01);

	lcd_send_cmd(spi, 0xC3);
	lcd_send_data(spi, 0x12);   

	lcd_send_cmd(spi, 0xC4);
	lcd_send_data(spi, 0x20);  

	lcd_send_cmd(spi, 0xC6); 
	lcd_send_data(spi, 0x0F);    

	lcd_send_cmd(spi, 0xD0); 
	lcd_send_data(spi, 0xA4);
	lcd_send_data(spi, 0xA1);

	lcd_send_cmd(spi, 0xE0);
	lcd_send_data(spi, 0xD0);
	lcd_send_data(spi, 0x04);
	lcd_send_data(spi, 0x0D);
	lcd_send_data(spi, 0x11);
	lcd_send_data(spi, 0x13);
	lcd_send_data(spi, 0x2B);
	lcd_send_data(spi, 0x3F);
	lcd_send_data(spi, 0x54);
	lcd_send_data(spi, 0x4C);
	lcd_send_data(spi, 0x18);
	lcd_send_data(spi, 0x0D);
	lcd_send_data(spi, 0x0B);
	lcd_send_data(spi, 0x1F);
	lcd_send_data(spi, 0x23);

	lcd_send_cmd(spi, 0xE1);
	lcd_send_data(spi, 0xD0);
	lcd_send_data(spi, 0x04);
	lcd_send_data(spi, 0x0C);
	lcd_send_data(spi, 0x11);
	lcd_send_data(spi, 0x13);
	lcd_send_data(spi, 0x2C);
	lcd_send_data(spi, 0x3F);
	lcd_send_data(spi, 0x44);
	lcd_send_data(spi, 0x51);
	lcd_send_data(spi, 0x2F);
	lcd_send_data(spi, 0x1F);
	lcd_send_data(spi, 0x1F);
	lcd_send_data(spi, 0x20);
	lcd_send_data(spi, 0x23);
	lcd_send_cmd(spi, 0x21); 

	lcd_send_cmd(spi, 0x29); 
} 

void create_tick(void* arg)
{
	while(1) {
		lv_tick_inc(5);
		lv_task_handler();
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}


static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
	u16 width = lv_area_get_width(area);
	u16 height = lv_area_get_width(area);
	//u16 *tmp = (u16 *)(gImage_pretty);

	lcd_flush(spi, area->x1, area->y1, width, height, (uint8_t *)color_p);

	lv_disp_flush_ready(disp_drv);
}

static void set_value(void *bar, int32_t v)
{
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
}

static void event_cb(lv_event_t * e)
{
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_param(e);
    if(dsc->part != LV_PART_INDICATOR) return;

    lv_obj_t * obj= lv_event_get_target(e);

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.font = LV_FONT_DEFAULT;

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", (int)lv_bar_get_value(obj));

    lv_point_t txt_size;
    lv_txt_get_size(&txt_size, buf, label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX, label_dsc.flag);

    lv_area_t txt_area;
    /*If the indicator is long enough put the text inside on the right*/
    if(lv_area_get_width(dsc->draw_area) > txt_size.x + 20) {
        txt_area.x2 = dsc->draw_area->x2 - 5;
        txt_area.x1 = txt_area.x2 - txt_size.x + 1;
        label_dsc.color = lv_color_white();
    }
    /*If the indicator is still short put the text out of it on the right*/
    else {
        txt_area.x1 = dsc->draw_area->x2 + 5;
        txt_area.x2 = txt_area.x1 + txt_size.x - 1;
        label_dsc.color = lv_color_black();
    }

    txt_area.y1 = dsc->draw_area->y1 + (lv_area_get_height(dsc->draw_area) - txt_size.y) / 2;
    txt_area.y2 = txt_area.y1 + txt_size.y - 1;

    lv_draw_label(dsc->draw_area, &label_dsc, &txt_area, buf, NULL);
}

/**
 * Custom drawer on the bar to display the current value
 */
void lv_example_bar_6(void)
{
#if 1
    lv_obj_t * bar = lv_bar_create(lv_scr_act());
    lv_obj_add_event_cb(bar, event_cb, LV_EVENT_DRAW_PART_END, NULL);
    lv_obj_set_size(bar, 200, 20);
    lv_obj_center(bar);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_exec_cb(&a, set_value);
    lv_anim_set_time(&a, 2000);
    lv_anim_set_playback_time(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
#endif
    lv_obj_t * label2 = lv_label_create(lv_scr_act());
    //lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    lv_obj_set_width(label2, 150);
    lv_label_set_text(label2, "LVGL TEST");
	lv_obj_center(label2);

    lv_obj_align(label2, LV_ALIGN_TOP_MID, 0, 0);

}


uint16_t color_arr[] = {BLACK, GREEN, BLUE, RED, YELLOW, BRRED};
#define DISP_BUF_SIZE 240*240
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf1[DISP_BUF_SIZE];

static void lvgl_disp_init(void)
{
	lv_disp_draw_buf_init(&disp_buf, buf1, NULL, DISP_BUF_SIZE);
	lv_disp_drv_init(&disp_drv);
	disp_drv.draw_buf = &disp_buf;
	disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
	disp_drv.flush_cb = disp_flush;
	lv_disp_drv_register(&disp_drv);
}


void app_main(void)
{

	//xTaskCreatePinnedToCore(myguiTask, "gui", 4096*2, NULL, 0, NULL, 1);
	esp_err_t ret;
	uint32_t x = 0, y = 0;
	int i = 0, j = 0;

	uint16_t buf_size = 240*16*sizeof(uint16_t);
	uint16_t *lines;
	uint8_t *tmp_p = NULL;
	static uint8_t frame = 0;

    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

	lcd_spi_init(HSPI_HOST);

	lcd_init();

	//lcd_fill_range(0, 0, 240, 240, SPI_SWAP_DATA_TX(BLACK, 16));

	lcd_flush(spi, 0, 0, 240, 240, (uint8_t *)gImage_pretty);
	vTaskDelay(2000 / portTICK_PERIOD_MS);

	lv_init();
	lvgl_disp_init();

	xTaskCreate(create_tick, "create_tick", 4096, NULL, 1, NULL);

#if 0
    lv_obj_t * bar1 = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar1, 200, 40);
    lv_obj_center(bar1);


    /*Copy the previous LED and set a brightness*/
    lv_obj_t * led2  = lv_led_create(lv_scr_act());
    lv_obj_align(led2, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_led_set_brightness(led2, 150);
    lv_led_set_color(led2, lv_palette_main(LV_PALETTE_BLUE));
#endif
	lv_example_bar_6();

    /*Fill the first column*/
	//lv_demo_widgets();


    while(1) {
		//lcd_flush(spi, 40, 100, 100, 100, gImage_tutu[i]);

		//lcd_flush(spi, 0, 0, 100, 50, gImage_xiong, 100*50*2*16);
		//lv_bar_set_value(bar1, x, LV_ANIM_ON);

        gpio_set_level(BLINK_GPIO, j);

        vTaskDelay(50 / portTICK_PERIOD_MS);
		j = !j;
		x ++;
		if (x > 99) x = 0;
    }

}


static void myguiTask(void *pvParameter)
{


}

