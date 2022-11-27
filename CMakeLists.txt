set(srcs
	"main.c"
	"disp/disp_drv.c"
	"disp/epd/epaper.c"
	"disp/epd/epapaer_spi.c"
	"disp/epd/epd_2in66.c"
	"net_time/net_time.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
					"disp"
					"disp/epd"
					"net_time")
