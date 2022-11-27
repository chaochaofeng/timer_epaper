
#ifndef _NET_TIME_H
#define _NET_TIME_H

#include "esp_http_client.h"

struct ntime_st {
	int16_t year;
	int16_t mon;
	int16_t day;
	int16_t week;
	int16_t hour;
	int16_t min;
	int16_t sec;
};

struct ntime_dev_st {
	char name[32];
	struct ntime_st ntime;
	esp_http_client_handle_t client;
	void (*init)(struct ntime_dev_st *);
	int (*set_config)(struct ntime_dev_st *);
	int (*get_time)(struct ntime_st *);
};

struct ntime_dev_st *ntime_get_dev(char *name);

#endif

