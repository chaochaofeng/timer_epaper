/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "freertos/event_groups.h"

#include "esp_http_client.h"
#include "net_time.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
static const char *TAG = "HTTP_CLIENT";

#define GET_TIME_DONE_EVENT        0x00000001

static EventGroupHandle_t wifi_events;


extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

extern const char postman_root_cert_pem_start[] asm("_binary_postman_root_cert_pem_start");
extern const char postman_root_cert_pem_end[]   asm("_binary_postman_root_cert_pem_end");

static const char *week_abb[] = {
	"Mon",
	"Tue",
	"Wed",
	"Thur",
	"Fri",
	"Sat",
	"Sun",
	NULL,
};

static const char *mon_abb[] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sept",
	"Oct",
	"Nov",
	"Dec",
};

static int16_t parse_item(char *week, const char *abb[])
{
	uint8_t i = 0;

	for (i = 0; abb[i]; i++) {
		if (strstr(week, abb[i]))
			return i+1;
	}

	return 0;
}

//Sun, 16 Oct 2022 14:12:06 GMT
static int parse_time(char *msg, struct ntime_st *ntime)
{
	char ctmp[5];
	char *bcp = NULL, *acp = NULL;

	acp = strstr(msg, ",");
	strncpy(ctmp, msg, acp - msg);
	ctmp[acp - msg] = '\0';
	ESP_LOGI(TAG, "week:%s", ctmp);
	ntime->week = parse_item(ctmp, week_abb);
	if (!ntime->week) {
		ESP_LOGE(TAG, "parse week err");
	}

	acp += 2;
	strncpy(ctmp, acp, 2);
	ctmp[2] = '\0';
	ESP_LOGI(TAG, "day:%s", ctmp);
	ntime->day = atoi(ctmp);
	if (!ntime->day) {
		ESP_LOGE(TAG, "parse day err");
	}

	acp += 3;
	bcp = acp;
	acp = strstr(bcp, " ");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "mon:%s", ctmp);
	ntime->mon = parse_item(ctmp, mon_abb);
	if (!ntime->mon) {
		ESP_LOGE(TAG, "parse mon err");
	}

	bcp = acp+1;
	acp = strstr(bcp, " ");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "year:%s", ctmp);
	ntime->year = atoi(ctmp);
	if (!ntime->year) {
		ESP_LOGE(TAG, "parse year err");
	}

	bcp = acp+1;
	acp = strstr(bcp, ":");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "hour:%s", ctmp);
	ntime->hour = atoi(ctmp);
	if (!ntime->hour) {
		ESP_LOGE(TAG, "parse hour err");
	}

	bcp = acp+1;
	acp = strstr(bcp, ":");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "min:%s", ctmp);
	ntime->min = atoi(ctmp);
	if (!ntime->min) {
		ESP_LOGE(TAG, "parse min err");
	}

	bcp = acp+1;
	acp = strstr(bcp, " ");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "sec:%s", ctmp);
	ntime->sec = atoi(ctmp);
	if (!ntime->min) {
		ESP_LOGE(TAG, "parse sec err");
	}

	return 0;
}

static bool is_leap_year(uint16_t year)
{
	if (year%4)
		return false;

	if (year % 100)
		return true;

	if (year % 400)
		return false;
	else
		return true;
}

static uint16_t cal_mon_days(uint16_t year, uint16_t mon)
{
	switch (mon) {
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
			return 31;
		case 2:
			if (is_leap_year(year))
				return 29;
			else
				return 28;
		default:
			return 30;
	}
}
static void ntime_gmt_to_beijing(struct ntime_st *ntime)
{
	uint16_t days = 0;
	printf("%s entry\n", __func__);

	ntime->hour += 8;
	if (ntime->hour < 24)
		goto out;

	ntime->hour %= 24;
	ntime->day++;

	days = cal_mon_days(ntime->year, ntime->mon);
	if (ntime->day <= days)
		goto out;

	ntime->day -= days;
	ntime->mon++;

	if (ntime->mon <= 12)
		goto out;

	ntime->year++;
out:
	ESP_LOGE(TAG, "date  %d-%d-%d %d:%d:%d", ntime->year,ntime->mon, ntime->day,
		ntime->hour, ntime->min, ntime->sec);
}

struct ntime_st ns;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    		if (strstr(evt->header_key,"Date")) {
				parse_time(evt->header_value, &ns);
				ntime_gmt_to_beijing(&ns);
			}
			break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
			xEventGroupSetBits(wifi_events, GET_TIME_DONE_EVENT);
            break;
    }
    return ESP_OK;
}

static void http_rest_with_url(void *pvParameters)
{
    esp_http_client_config_t config = {
        .url = "https://www.baidu.com",
        .event_handler = _http_event_handler,
        .user_data = pvParameters,        // Pass address of local buffer to get response
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

	ESP_LOGE(TAG, "%s ns %p", __func__, config.user_data);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void http_test_task(void *pvParameters)
{
	ESP_LOGE(TAG, "%s ns %p", __func__, pvParameters);

    http_rest_with_url(pvParameters);

    vTaskDelete(NULL);
}

static void app_wifi_init(struct ntime_dev_st *ntime)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
	if (wifi_events == NULL) {
        wifi_events = xEventGroupCreate();
    } else {
        xEventGroupClearBits(wifi_events, 0x00ffffff);
    }
}
static int app_wifi_get_time(struct ntime_st *ntime);

struct ntime_dev_st wifi_time = {
	.name = "wifi_time",
	.init = app_wifi_init,
	.get_time = app_wifi_get_time,
};

static int app_wifi_get_time(struct ntime_st *ntime)
{
	EventBits_t bits;
	ESP_LOGE(TAG, "%s ns %p", __func__, ntime);

	ESP_ERROR_CHECK(example_connect());

    xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);
    bits = xEventGroupWaitBits(wifi_events, GET_TIME_DONE_EVENT, 1, 0, 7000/portTICK_RATE_MS);
    if (bits == GET_TIME_DONE_EVENT) {
		ESP_LOGI(TAG, "GET_TIME_DONE");
		xEventGroupClearBits(wifi_events, GET_TIME_DONE_EVENT);
	}
	ESP_ERROR_CHECK(example_disconnect());
	ESP_LOGE(TAG, "date %d:%d:%d", ns.hour, ns.min, ns.sec);

	ntime->hour = ns.hour;
	ntime->min = ns.min;
	ntime->sec = ns.sec;
	return 0;
}

struct ntime_dev_st *ntime_list[] = {
	&wifi_time,
};

struct ntime_dev_st *ntime_get_dev(char *name)
{

	for (int i=0; i < sizeof(ntime_list)/sizeof(ntime_list[0]); i++) {
		if (strcmp(name, ntime_list[i]->name)) {
			continue;
		}

		return ntime_list[i];
	}

	printf("time: not support dev:%s\n", name);
	return NULL;
}

#if 0
#include "string.h"
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "esp_tls.h"

#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "protocol_examples_common.h"

#include "net_time.h"

#define TAG "wifi_time"

#define GET_TIME_DONE_EVENT        0x00000001

static EventGroupHandle_t wifi_events;


static const char *week_abb[] = {
	"Mon",
	"Tue",
	"Wed",
	"Thur",
	"Fri",
	"Sat",
	"Sun",
	NULL,
};

static const char *mon_abb[] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sept",
	"Oct",
	"Nov",
	"Dec",
};

static int16_t parse_item(char *week, const char *abb[])
{
	uint8_t i = 0;

	for (i = 0; abb[i]; i++) {
		if (strstr(week, abb[i]))
			return i+1;
	}

	return 0;
}

//Sun, 16 Oct 2022 14:12:06 GMT
static int parse_time(char *msg, struct ntime_st *ntime)
{
	char ctmp[5];
	char *bcp = NULL, *acp = NULL;

	acp = strstr(msg, ",");
	strncpy(ctmp, msg, acp - msg);
	ctmp[acp - msg] = '\0';
	ESP_LOGI(TAG, "week:%s", ctmp);
	ntime->week = parse_item(ctmp, week_abb);
	if (!ntime->week) {
		ESP_LOGE(TAG, "parse week err");
	}

	acp += 2;
	strncpy(ctmp, acp, 2);
	ctmp[2] = '\0';
	ESP_LOGI(TAG, "day:%s", ctmp);
	ntime->day = atoi(ctmp);
	if (!ntime->day) {
		ESP_LOGE(TAG, "parse day err");
	}

	acp += 3;
	bcp = acp;
	acp = strstr(bcp, " ");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "mon:%s", ctmp);
	ntime->mon = parse_item(ctmp, mon_abb);
	if (!ntime->mon) {
		ESP_LOGE(TAG, "parse mon err");
	}

	bcp = acp+1;
	acp = strstr(bcp, " ");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "year:%s", ctmp);
	ntime->year = atoi(ctmp);
	if (!ntime->year) {
		ESP_LOGE(TAG, "parse year err");
	}

	bcp = acp+1;
	acp = strstr(bcp, ":");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "hour:%s", ctmp);
	ntime->hour = atoi(ctmp);
	if (!ntime->hour) {
		ESP_LOGE(TAG, "parse hour err");
	}

	bcp = acp+1;
	acp = strstr(bcp, ":");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "min:%s", ctmp);
	ntime->min = atoi(ctmp);
	if (!ntime->min) {
		ESP_LOGE(TAG, "parse min err");
	}

	bcp = acp+1;
	acp = strstr(bcp, " ");
	strncpy(ctmp, bcp, acp - bcp);
	ctmp[acp - bcp] = '\0';
	ESP_LOGI(TAG, "sec:%s", ctmp);
	ntime->sec = atoi(ctmp);
	if (!ntime->min) {
		ESP_LOGE(TAG, "parse sec err");
	}

	return 0;
}

static bool is_leap_year(uint16_t year)
{
	if (year%4)
		return false;

	if (year % 100)
		return true;

	if (year % 400)
		return false;
	else
		return true;
}

static uint16_t cal_mon_days(uint16_t year, uint16_t mon)
{
	switch (mon) {
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
			return 31;
		case 2:
			if (is_leap_year(year))
				return 29;
			else
				return 28;
		default:
			return 30;
	}
}
static void ntime_gmt_to_beijing(struct ntime_st *ntime)
{
	uint16_t days = 0;

	ntime->hour += 8;
	if (ntime->hour < 24)
		return;

	ntime->hour %= 24;
	ntime->day++;

	days = cal_mon_days(ntime->year, ntime->mon);
	if (ntime->day <= days)
		return;

	ntime->day -= days;
	ntime->mon++;

	if (ntime->mon <= 12)
		return;

	ntime->year++;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	static char *output_buffer;  // Buffer to store response of http request from event handler
	static int output_len;		 // Stores number of bytes read
	
	struct ntime_dev_st *ntime = (struct ntime_dev_st *)evt->user_data;

	switch(evt->event_id) {
		case HTTP_EVENT_ERROR:
			ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
			break;
		case HTTP_EVENT_HEADER_SENT:
			ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
			break;
		case HTTP_EVENT_ON_HEADER:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
			if (strstr(evt->header_key,"Date")) {
				parse_time(evt->header_value, &ntime->ntime);
				ntime_gmt_to_beijing(&ntime->ntime);
			}
			break;
		case HTTP_EVENT_ON_DATA:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			/*
			 *	Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
			 *	However, event handler can also be used in case chunked encoding is used.
			 */
			if (!esp_http_client_is_chunked_response(evt->client)) {
				// If user_data buffer is configured, copy the response into the buffer
				if (evt->user_data) {
					memcpy(evt->user_data + output_len, evt->data, evt->data_len);
				} else {
					if (output_buffer == NULL) {
						output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
						output_len = 0;
						if (output_buffer == NULL) {
							ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
							return ESP_FAIL;
						}
					}
					memcpy(output_buffer + output_len, evt->data, evt->data_len);
					//ESP_LOGI(TAG, "ON_DATA:%s", (char *)evt->data);
				}
				output_len += evt->data_len;
			}

			break;
		case HTTP_EVENT_ON_FINISH:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
			if (output_buffer != NULL) {
				// Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
				// ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
				free(output_buffer);
				output_buffer = NULL;
			}
			output_len = 0;
			break;
		case HTTP_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
			int mbedtls_err = 0;
			esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
			if (err != 0) {
				if (output_buffer != NULL) {
					free(output_buffer);
					output_buffer = NULL;
				}
				output_len = 0;
				ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
				ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
			}
			xEventGroupSetBits(wifi_events, GET_TIME_DONE_EVENT);
			break;
	}
	return ESP_OK;
}

static void http_rest_with_url(void *pvParameters)
{
    struct ntime_dev_st *ntime = (struct ntime_dev_st *)pvParameters;
    esp_http_client_config_t config = {
        .url = "https://www.baidu.com",
        .event_handler = _http_event_handler,
        .user_data = pvParameters,        // Pass address of local buffer to get response
    };
    ntime->client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(ntime->client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(ntime->client),
                esp_http_client_get_content_length(ntime->client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(ntime->client);
}

static void http_test_task(void *pvParameters)
{
	http_rest_with_url(pvParameters);

	vTaskDelete(NULL);
}

static void app_wifi_init(struct ntime_dev_st *ntime)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK(example_connect());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    //ESP_ERROR_CHECK(example_connect());
    //ESP_LOGI(TAG, "Connected to AP, begin http example");

	if (wifi_events == NULL) {
        wifi_events = xEventGroupCreate();
    } else {
        xEventGroupClearBits(wifi_events, 0x00ffffff);
    }
}

static int app_wifi_get_time(struct ntime_dev_st *ntime)
{
	EventBits_t bits;

    xTaskCreate(&http_test_task, "http_test_task", 8192, (void *)ntime, 5, NULL);
//    bits = xEventGroupWaitBits(wifi_events, GET_TIME_DONE_EVENT, 1, 0, 7000/portTICK_RATE_MS);
//    if (bits == GET_TIME_DONE_EVENT) {
//		ESP_LOGI(TAG, "GET_TIME_DONE");
//		xEventGroupClearBits(wifi_events, GET_TIME_DONE_EVENT);
//	}

	//example_disconnect();

	return 0;
}


struct ntime_dev_st wifi_time = {
	.name = "wifi_time",
	.init = app_wifi_init,
	.get_time = app_wifi_get_time,
};


struct ntime_dev_st *ntime_list[] = {
	&wifi_time,
};

struct ntime_dev_st *ntime_get_dev(char *name)
{

	for (int i=0; i < sizeof(ntime_list)/sizeof(ntime_list[0]); i++) {
		if (strcmp(name, ntime_list[i]->name)) {
			continue;
		}

		return ntime_list[i];
	}

	printf("time: not support dev:%s\n", name);
	return NULL;
}
#endif
