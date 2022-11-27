#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/timer.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "disp_drv.h"
#include "net_time.h"
#include "c_number.h"
#include "epd_2in66.h"

#define PIN_NUM_LED 2
#define mdelay(ms) vTaskDelay(ms / portTICK_RATE_MS)

static void led_init(void)
{
	gpio_reset_pin(PIN_NUM_LED);
			/* Set the GPIO as a push/pull output */
	gpio_set_direction(PIN_NUM_LED, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_LED, 0);
}

struct epaper_time_st {
	unsigned char hour;
	unsigned char min;
	unsigned char sec;
	unsigned char hours; //24 or 12
	bool update_flag;
	timer_config_t alarm_config;
	bool start_calibrat;
};

static void epaper_ntime_convert_time(struct epaper_time_st *e_time, struct ntime_st *ntime)
{
	e_time->hours = 24;
	e_time->hour  = ntime->hour;
	e_time->min   = ntime->min;
	e_time->sec   = ntime->sec;
	e_time->update_flag = true;
	e_time->start_calibrat = false;
	ESP_LOGE("haah", "date %d:%d:%d", e_time->hour, e_time->min, e_time->sec);
}

static void epaper_time_update(struct epaper_time_st *time)
{
	time->sec++;
	if (time->sec >= 60) {
		time->sec = 0;
		time->min++;
		time->update_flag = 1;
	}

	if (time->min >= 60) {
		time->min = 0;
		time->hour++;
	}

	if (time->hour >= time->hours) {
		time->hour = 0;
		time->start_calibrat = true;
	}
}

static bool timer_callback(void* arg)
{
	struct epaper_time_st *e_time = (struct epaper_time_st *)arg;

	epaper_time_update(e_time);

	return pdTRUE;
}

static void my_timer_init(struct epaper_time_st *e_time, struct ntime_st *ntime)
{
	timer_config_t *alarm_config = &e_time->alarm_config;

	e_time->hours = 24;
	e_time->hour  = ntime->hour;
	e_time->min   = ntime->min;
	e_time->sec   = ntime->sec;
	e_time->update_flag = true;
	e_time->start_calibrat = true;

	alarm_config->alarm_en = TIMER_ALARM_EN,
	alarm_config->counter_dir = TIMER_COUNT_UP,
	alarm_config->counter_en = TIMER_PAUSE,
	alarm_config->auto_reload = TIMER_AUTORELOAD_EN,
	alarm_config->divider = 8000,
	alarm_config->intr_type = TIMER_INTR_LEVEL,

	timer_init(TIMER_GROUP_0, TIMER_0, alarm_config);
	timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
	timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 10000);
	timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_callback,(void *)e_time, 0);

	timer_start(TIMER_GROUP_0, TIMER_0);
}
static void timer_update(struct disp_dev_st *ddev, unsigned char hour, unsigned char min)
{
	unsigned char ten_bit = 0, indivual_bit = 0;
	unsigned char x_offset = 0, y_offset = 0;

	if (hour > 23)	hour = 0;

	if (min > 59)	min = 0;

	ten_bit = hour / 10;
	indivual_bit = hour % 10;
#if 1
	x_offset += 16;

	/* hour ten bit */
	disp_flush(ddev, 0, x_offset, 144, 56, (unsigned char *)c_number_18x56[ten_bit]);

	/* hour indivual bit */
	x_offset += 57;
	disp_flush(ddev, 0, x_offset, 144, 56, (unsigned char *)c_number_18x56[indivual_bit]);

	x_offset += 57;
	x_offset += 7;
	y_offset += 40;

	disp_flush(ddev, y_offset, x_offset, 72, 9, (unsigned char *)c_colon);

	ten_bit = min / 10;
	indivual_bit = min % 10;

	x_offset += 7;
	x_offset += 9;
	disp_flush(ddev, 0, x_offset, 144, 56, (unsigned char *)c_number_18x56[ten_bit]);
	x_offset += 57;
	disp_flush(ddev, 0, x_offset, 144, 56, (unsigned char *)c_number_18x56[indivual_bit]);

#else
	x_offset += 16;

	/* hour ten bit */
	disp_flush(ddev, 0, x_offset, 104, 36, (unsigned char *)c_number_13x36B[ten_bit]);

	/* hour indivual bit */
	x_offset += 40;
	disp_flush(ddev, 0, x_offset, 104, 36, (unsigned char *)c_number_13x36B[indivual_bit]);

	x_offset += 36;
	x_offset += 7;
	y_offset += 24;

	disp_flush(ddev, y_offset, x_offset, 56, 9, (unsigned char *)c_colon);

	ten_bit = min / 10;
	indivual_bit = min % 10;

	x_offset += 7;
	x_offset += 9;
	disp_flush(ddev, 0, x_offset, 104, 36, (unsigned char *)c_number_13x36B[ten_bit]);
	x_offset += 40;
	disp_flush(ddev, 0, x_offset, 104, 36, (unsigned char *)c_number_13x36B[indivual_bit]);
#endif
	disp_on(ddev);
}

static void time_app_task(void *pvParameters)
{
	struct disp_dev_st *ddev = NULL;
	struct ntime_dev_st *ntime_dev = NULL;
	struct epaper_time_st e_time;
	struct ntime_st ns;

	int cont = 0;
	led_init();

	ddev = disp_init("epd_2in66");
	if (!ddev) {
		printf("ddev init err\n");
		return;
	}
	//ddev->drv->clear_screen(ddev);
	ddev->draw_color = 0;

	ntime_dev = ntime_get_dev("wifi_time");
	if (!ntime_dev) {
		printf("ntime_dev init err\n");
		return;
	}

	ntime_dev->init(ntime_dev);

	my_timer_init(&e_time, &ntime_dev->ntime);

	while(1) {
		if (!ddev->drv)
			ESP_LOGE("temp", "ddev is null");
		if (e_time.start_calibrat) {
			ntime_dev->get_time(&ns);
			epaper_ntime_convert_time(&e_time, &ns);
		}

		if (e_time.update_flag) {
			timer_update(ddev, e_time.hour, e_time.min);
			e_time.update_flag = 0;
		}

		gpio_set_level(PIN_NUM_LED, 0);
		mdelay(1000);
		gpio_set_level(PIN_NUM_LED, 1);
		mdelay(1000);
	}

}

static void test_task(void *pvParameters)
{
	epd_2in66_dev_init(152, 296);
	epd_2in66_clear();

	while(1) {
		gpio_set_level(PIN_NUM_LED, 0);
		mdelay(1000);
		gpio_set_level(PIN_NUM_LED, 1);
		mdelay(1000);
	}
}

void app_main(void)
{
	xTaskCreate(&time_app_task, "time_app_task", 10240, NULL, 5, NULL);
	//xTaskCreate(&test_task, "test_task", 8196, NULL, 5, NULL);
}

