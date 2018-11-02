// Copyright 2015-2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "camera.h"
#include "bitmap.h"
#include "http_server.h"

#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

#include "esp_wpa2.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"

#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"

#define BIT_0	( 1 << 0 )
#define BIT_4	( 1 << 4 )
//WIFI_MANAGER
#define WIFI_MANAGER_DEBUG	1

#define MAX_SSID_SIZE		32
#define MAX_PASSWORD_SIZE	64
#define MAX_TEMP_FIELD_SIZE	60
#define MAX_AP_NUM 			15
#define AP_AUTHMODE 		WIFI_AUTH_WPA2_PSK
#define DEFAULT_SEVER_PORT 		80

#define DEFAULT_SERVER_IP 			"192.168.1.83"
#define DEFAULT_SERVER_IP 			"192.168.1.83"
#define DEFAULT_ARRAY 				"0"
#define DEFAULT_AP_BANDWIDTH 		WIFI_BW_HT20
#define AP_MAX_CONNECTIONS 	4
#define DEFAULT_DEEPSLEEP	20
#define AP_BEACON_INTERVAL 	100
#define DEFAULT_STA_ONLY 			1
#define DEFAULT_STA_POWER_SAVE 			WIFI_PS_MODEM
#define EXAMPLE_ESP_WIFI_MODE_AP CONFIG_ESP_WIFI_MODE_STA  // TRUE:AP FALSE:STA
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_MAX_STA_CONN CONFIG_MAX_STA_CONN
#define CAMERA_LED_GPIO 16

#define CAMERA_PIXEL_FORMAT CAMERA_PF_RGB565 //org CAMERA_PF_GRAYSCALE
#define CAMERA_FRAME_SIZE CAMERA_FS_SVGA


#define WEB_SERVER "192.168.1.83"
#define WEB_PORT 80		
#define MAX_RETRY 10	


/**
 * The actual WiFi settings in use
 */
struct wifi_settings_t{
	uint8_t server_ip[MAX_SSID_SIZE];
	uint32_t deepsheep_time;					//TOMATO max_sleep long  4
	uint8_t temp_field[MAX_TEMP_FIELD_SIZE];    //TOMATO temp_field      60
	uint8_t temp_cnt;
	uint8_t server_port;
	wifi_bandwidth_t ap_bandwidth;
	bool boot_normal;
	wifi_ps_type_t sta_power_save;
	bool sta_static_ip;
	tcpip_adapter_ip_info_t server_ip_config;
};
extern struct wifi_settings_t nvs_wifi_settings; 

struct wifi_settings_t nvs_wifi_settings = {
	.server_ip = DEFAULT_SERVER_IP,
	.deepsheep_time = DEFAULT_DEEPSLEEP,
	.temp_field = {0,},
	.temp_cnt = 0,
	.server_port = DEFAULT_SEVER_PORT,
	.ap_bandwidth = DEFAULT_AP_BANDWIDTH,
	.boot_normal = DEFAULT_STA_ONLY,
	.sta_power_save = DEFAULT_STA_POWER_SAVE,
	.sta_static_ip = 0,
};

// globle Var.S
wifi_config_t* wifi_manager_config_sta = NULL;
const char wifi_manager_nvs_namespace[] = "espwifimgr";
static void wifi_init_sta(void);
int wifi_mode = 0;
static const char* TAG = "dekkio_app";

static const char* STREAM_CONTENT_TYPE =
        "multipart/x-mixed-replace; boundary=123456789000000000000987654321";

static const char* STREAM_BOUNDARY = "--123456789000000000000987654321";

static EventGroupHandle_t s_wifi_event_group;
const int CONNECTED_BIT = BIT0;
static ip4_addr_t s_ip_addr;
static camera_pixelformat_t s_pixel_format;

static const char *REQUEST = "POST /api/device/image HTTP/1.1\r\n"
				"Content-Length: 23200\r\n"
               "Content-Type: image/jpeg\r\n"
      		    "\r\n";	

static const char *REQUEST_POST = "POST /api/device/image HTTP/1.1\r\n";
static const char *REQUEST_LEN = "Content-Length:";
static const char *REQUEST_TYPE = "Content-Type: image/jpeg\r\n";
static const char *REQUEST_END = "\r\n";

/* Block for 10sec. */
const TickType_t xDelay = 10*1000 / portTICK_PERIOD_MS;
EventBits_t uxBits;   //xEventGroupWaitBits
int  retry_count = 0; 
// globle Var.E

///FUNC.head.S
static void handle_grayscale_pgm(http_context_t http_ctx, void* ctx);
static void handle_rgb_bmp(http_context_t http_ctx, void* ctx);
static void handle_rgb_bmp_stream(http_context_t http_ctx, void* ctx);
static void handle_jpg(http_context_t http_ctx, void* ctx);
static void handle_jpg_stream(http_context_t http_ctx, void* ctx);
static esp_err_t event_handler(void *ctx, system_event_t *event);
esp_err_t wifi_manager_save_nvs_wifi_config();
bool wifi_manager_read_nvs_wifi_config();
void LED_TaskDelay(int param);
void InitVal();
void deepsleep_start();
static void smartconfig_wifi_start(void);
///FUNC.head.E

void  start_httpd()
{
	http_server_t server;
    http_server_options_t http_options = HTTP_SERVER_OPTIONS_DEFAULT();
    ESP_ERROR_CHECK( http_server_start(&http_options, &server) );

    if (s_pixel_format == CAMERA_PF_GRAYSCALE) {
        ESP_ERROR_CHECK( http_register_handler(server, "/pgm", HTTP_GET, HTTP_HANDLE_RESPONSE, &handle_grayscale_pgm, NULL) );
        ESP_LOGI(TAG, "Open http://" IPSTR "/pgm for a single image/x-portable-graymap image", IP2STR(&s_ip_addr));
    }
    if (s_pixel_format == CAMERA_PF_RGB565) {
        ESP_ERROR_CHECK( http_register_handler(server, "/bmp", HTTP_GET, HTTP_HANDLE_RESPONSE, &handle_rgb_bmp, NULL) );
        ESP_LOGI(TAG, "Open http://" IPSTR "/bmp for single image/bitmap image", IP2STR(&s_ip_addr));
        ESP_ERROR_CHECK( http_register_handler(server, "/bmp_stream", HTTP_GET, HTTP_HANDLE_RESPONSE, &handle_rgb_bmp_stream, NULL) );
        ESP_LOGI(TAG, "Open http://" IPSTR "/bmp_stream for multipart/x-mixed-replace stream of bitmaps", IP2STR(&s_ip_addr));
    }
    if (s_pixel_format == CAMERA_PF_JPEG) {
        ESP_ERROR_CHECK( http_register_handler(server, "/jpg", HTTP_GET, HTTP_HANDLE_RESPONSE, &handle_jpg, NULL) );
        ESP_LOGI(TAG, "Open http://" IPSTR "/jpg for single image/jpg image", IP2STR(&s_ip_addr));
        ESP_ERROR_CHECK( http_register_handler(server, "/jpg_stream", HTTP_GET, HTTP_HANDLE_RESPONSE, &handle_jpg_stream, NULL) );
        ESP_LOGI(TAG, "Open http://" IPSTR "/jpg_stream for multipart/x-mixed-replace stream of JPEGs", IP2STR(&s_ip_addr));
        ESP_ERROR_CHECK( http_register_handler(server, "/", HTTP_GET, HTTP_HANDLE_RESPONSE, &handle_jpg_stream, NULL) );
    }
    ESP_LOGI(TAG, "Free heap: %u", xPortGetFreeHeapSize());
    ESP_LOGI(TAG, "Camera demo ready");		
}

//////////////////////////////////////////////
//SMART.S
/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;
static const int ESPTOUCH_DONE_BIT = BIT1;
void smartconfig_example_task(void * parm);

static void smartconfig_wifi_start(void)
{
    tcpip_adapter_init();
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void sc_callback(smartconfig_status_t status, void *pdata)
{
	int sz = 0;
	char *token[4];
	const char que[2] = ":";
	
	char *token_ip[4];
	const char dot[2] = ".";
	int i = 0;
	
    switch (status) {
        case SC_STATUS_WAIT:
            ESP_LOGI(TAG, "SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            ESP_LOGI(TAG, "SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
            ESP_LOGI(TAG, "SC_STATUS_LINK");
            wifi_config_t *wifi_config = pdata;
			
            ESP_LOGI(TAG, "SSID:%s", wifi_config->sta.ssid);
			/////////////////////////////////////////////////////////////
			ESP_LOGI(TAG, "PASSWORD_ORG:%s", wifi_config->sta.password);
			token[0] = strtok((char *)wifi_config->sta.password, que);
			while( token[i] != NULL ) {
		       //printf("%s\n",token[i] );
				i++;
				token[i] = strtok(NULL, que);
			}
			sz = sizeof(token[0]);		
			memcpy(wifi_config->sta.password, token[0], sz);
		    sz = sizeof(token[1]);
			memcpy(wifi_manager_config_sta->sta.ssid, token[1], sz);
			
			nvs_wifi_settings.server_port =  atoi(token[2]);
			nvs_wifi_settings.deepsheep_time = atol(token[3]);
				
			ESP_LOGI(TAG, "PASSWORD:%s", wifi_config->sta.password);			
			ESP_LOGI(TAG, "port:%i",  nvs_wifi_settings.server_port );
			ESP_LOGI(TAG, "delay: 0x%08x", nvs_wifi_settings.deepsheep_time);	
#if 1			
			//IP_ADDRESS
			i = 0;
			token_ip[0] = strtok((char *)token[1], dot);
			while( token_ip[i] != NULL ) {
		       //printf("%s\n",token_ip[i] );
				i++;
				token_ip[i] = strtok(NULL, dot);
			}			
			IP4_ADDR(&nvs_wifi_settings.server_ip_config.ip, atoi(token_ip[0]), atoi(token_ip[1]), atoi(token_ip[2]), atoi(token_ip[3]));
			ESP_LOGI(TAG, "server_ip:%s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.ip) );
#endif			
			/////////////////////////////////////////////////////////////
           
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );			

			memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));		
			sz = sizeof(wifi_manager_config_sta->sta.ssid);
			memcpy(wifi_manager_config_sta->sta.ssid, wifi_config->sta.ssid, sz);
			sz = sizeof(wifi_manager_config_sta->sta.password);
			memcpy(wifi_manager_config_sta->sta.password, wifi_config->sta.password, sz);
			
			nvs_wifi_settings.boot_normal = 1;
			wifi_manager_save_nvs_wifi_config();
			ESP_LOGI(TAG, "SC_SAVE CONFIG");
            break;
        case SC_STATUS_LINK_OVER:
            ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = { 0 };
                memcpy(phone_ip, (uint8_t* )pdata, 4);
                ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);

            break;
        default:
            break;
    }
}

void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
			esp_sleep_enable_timer_wakeup(1000*1000);
			ESP_LOGI(TAG, "== End App! == ");
			esp_deep_sleep_start();
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}
//SMART.E
//////////////////////////////////////////////


static esp_err_t write_frame(http_context_t http_ctx)
{
    http_buffer_t fb_data = {
            .data = camera_get_fb(),
            .size = camera_get_data_size(),
            .data_is_persistent = true
    };
    return http_response_write(http_ctx, &fb_data);
}

static void handle_grayscale_pgm(http_context_t http_ctx, void* ctx)
{
    esp_err_t err = camera_run();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
        return;
    }
    char* pgm_header_str;
    asprintf(&pgm_header_str, "P5 %d %d %d\n",
            camera_get_fb_width(), camera_get_fb_height(), 255);
    if (pgm_header_str == NULL) {
        return;
    }

    size_t response_size = strlen(pgm_header_str) + camera_get_data_size();
    http_response_begin(http_ctx, 200, "image/x-portable-graymap", response_size);
    http_response_set_header(http_ctx, "Content-disposition", "inline; filename=capture.pgm");
    http_buffer_t pgm_header = { .data = pgm_header_str };
    http_response_write(http_ctx, &pgm_header);
    free(pgm_header_str);

    write_frame(http_ctx);
    http_response_end(http_ctx);
}

static void handle_rgb_bmp(http_context_t http_ctx, void* ctx)
{
    esp_err_t err = camera_run();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
        return;
    }

    bitmap_header_t* header = bmp_create_header(camera_get_fb_width(), camera_get_fb_height());
    if (header == NULL) {
        return;
    }

    http_response_begin(http_ctx, 200, "image/bmp", sizeof(*header) + camera_get_data_size());
    http_buffer_t bmp_header = {
            .data = header,
            .size = sizeof(*header)
    };
    http_response_set_header(http_ctx, "Content-disposition", "inline; filename=capture.bmp");
    http_response_write(http_ctx, &bmp_header);
    free(header);

    write_frame(http_ctx);
    http_response_end(http_ctx);
}

static void handle_jpg(http_context_t http_ctx, void* ctx)
{
    esp_err_t err = camera_run();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
        return;
    }

    http_response_begin(http_ctx, 200, "image/jpeg", camera_get_data_size());
    http_response_set_header(http_ctx, "Content-disposition", "inline; filename=capture.jpg");
    write_frame(http_ctx);
    http_response_end(http_ctx);
}


static void handle_rgb_bmp_stream(http_context_t http_ctx, void* ctx)
{
    http_response_begin(http_ctx, 200, STREAM_CONTENT_TYPE, HTTP_RESPONSE_SIZE_UNKNOWN);
    bitmap_header_t* header = bmp_create_header(camera_get_fb_width(), camera_get_fb_height());
    if (header == NULL) {
        return;
    }
    http_buffer_t bmp_header = {
            .data = header,
            .size = sizeof(*header)
    };


    while (true) {
        esp_err_t err = camera_run();
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
            return;
        }

        err = http_response_begin_multipart(http_ctx, "image/bitmap",
                camera_get_data_size() + sizeof(*header));
        if (err != ESP_OK) {
            break;
        }
        err = http_response_write(http_ctx, &bmp_header);
        if (err != ESP_OK) {
            break;
        }
        err = write_frame(http_ctx);
        if (err != ESP_OK) {
            break;
        }
        err = http_response_end_multipart(http_ctx, STREAM_BOUNDARY);
        if (err != ESP_OK) {
            break;
        }
    }

    free(header);
    http_response_end(http_ctx);
}

static void handle_jpg_stream(http_context_t http_ctx, void* ctx)
{
    http_response_begin(http_ctx, 200, STREAM_CONTENT_TYPE, HTTP_RESPONSE_SIZE_UNKNOWN);

    while (true) {
        esp_err_t err = camera_run();
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
            return;
        }
        err = http_response_begin_multipart(http_ctx, "image/jpg",
                camera_get_data_size());
        if (err != ESP_OK) {
            break;
        }
        err = write_frame(http_ctx);
        if (err != ESP_OK) {
            break;
        }
        err = http_response_end_multipart(http_ctx, STREAM_BOUNDARY);
        if (err != ESP_OK) {
            break;
        }
    }
    http_response_end(http_ctx);
}


// /* FreeRTOS event group to signal when we are connected*/
// static EventGroupHandle_t s_wifi_event_group;

// /* The event group allows multiple bits for each event,
//    but we only care about one event - are we connected
//    to the AP with an IP? */
// const int WIFI_CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void* ctx, system_event_t* event) 
{
  switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
		if(wifi_mode == 1)
			esp_wifi_connect();
		else{	
			//SMART.S
			xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
			//SMART.E
		}
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
      s_ip_addr = event->event_info.got_ip.ip_info.ip;
      xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
	  //if(wifi_mode == 1)
	  //  start_httpd();
      break;
    case SYSTEM_EVENT_AP_STACONNECTED:
      ESP_LOGI(TAG, "station:" MACSTR " join, AID=%d", MAC2STR(event->event_info.sta_connected.mac),
               event->event_info.sta_connected.aid);
#if EXAMPLE_ESP_WIFI_MODE_AP
      xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
#endif
      break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
      ESP_LOGI(TAG, "station:" MACSTR "leave, AID=%d", MAC2STR(event->event_info.sta_disconnected.mac),
               event->event_info.sta_disconnected.aid);
#if EXAMPLE_ESP_WIFI_MODE_AP
      xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
#endif
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      esp_wifi_connect();
      xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
      break;
    default:
      break;
  }
  return ESP_OK;
}

static RTC_DATA_ATTR struct timeval sleep_enter_time;

////////////////
// START NVS
bool wifi_manager_read_nvs_wifi_config(){

	nvs_handle handle;
	esp_err_t esp_err;
	if(nvs_open(wifi_manager_nvs_namespace, NVS_READONLY, &handle) == ESP_OK){

		if(wifi_manager_config_sta == NULL){
			wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
		}
		memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
		memset(&nvs_wifi_settings, 0x00, sizeof(struct wifi_settings_t));

		/* allocate buffer */
		size_t sz = sizeof(nvs_wifi_settings);
		uint8_t *buff = (uint8_t*)malloc(sizeof(uint8_t) * sz);
		memset(buff, 0x00, sizeof(sz));

		/* ssid */
		sz = sizeof(wifi_manager_config_sta->sta.ssid);
		esp_err = nvs_get_blob(handle, "ssid", buff, &sz);
		if(esp_err != ESP_OK){
			free(buff);
			return false;
		}
		memcpy(wifi_manager_config_sta->sta.ssid, buff, sz);

		/* password */
		sz = sizeof(wifi_manager_config_sta->sta.password);
		esp_err = nvs_get_blob(handle, "password", buff, &sz);
		if(esp_err != ESP_OK){
			free(buff);
			return false;
		}
		memcpy(wifi_manager_config_sta->sta.password, buff, sz);

		/* settings */
		sz = sizeof(nvs_wifi_settings);
		esp_err = nvs_get_blob(handle, "settings", buff, &sz);
		if(esp_err != ESP_OK){
			free(buff);
			return false;
		}
		memcpy(&nvs_wifi_settings, buff, sz);

		free(buff);
		nvs_close(handle);

#if WIFI_MANAGER_DEBUG
		ESP_LOGI(TAG, "Read_config: ssid:%s password: %s ",wifi_manager_config_sta->sta.ssid,wifi_manager_config_sta->sta.password);
		ESP_LOGI(TAG, "Read_config: boot_normal (0 = SC, 1 = STA when connected): %i",nvs_wifi_settings.boot_normal);
		ESP_LOGI(TAG, "Read_config: deepsheep_time: 0x%08x ",nvs_wifi_settings.deepsheep_time);
#if 0
		ESP_LOGI(TAG, "Read_config: sta_static_ip (0 = dhcp client, 1 = static ip):%i",nvs_wifi_settings.sta_static_ip);
		ESP_LOGI(TAG, "Read_config: sta_ip_addr: %s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.ip));
		ESP_LOGI(TAG, "Read_config: SoftAP_pwd:%s",nvs_wifi_settings.ap_pwd);
		
		ESP_LOGI(TAG, "Read_config: SoftAP_hidden (1 = yes):%i",nvs_wifi_settings.ap_ssid_hidden);
		ESP_LOGI(TAG, "Read_config: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz)%i",nvs_wifi_settings.ap_bandwidth);		
		ESP_LOGI(TAG, "Read_config: sta_power_save (1 = yes):%i",nvs_wifi_settings.sta_power_save);
		ESP_LOGI(TAG, "Read_config: sta_gw_addr: %s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.gw));
		ESP_LOGI(TAG, "Read_config: sta_netmask: %s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.netmask));
#endif
		if(nvs_wifi_settings.boot_normal == 0)
			return false;
#endif
		return wifi_manager_config_sta->sta.ssid[0] != '\0';


	}
	else{
		return false;
	}

}

esp_err_t wifi_manager_save_nvs_wifi_config(){

	nvs_handle handle;
	esp_err_t esp_err;
#if WIFI_MANAGER_DEBUG
	//printf("wifi_manager: About to save config to flash\n");
#endif
	if(wifi_manager_config_sta){

		esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "ssid", wifi_manager_config_sta->sta.ssid, 32);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "password", wifi_manager_config_sta->sta.password, 64);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "settings", &nvs_wifi_settings, sizeof(nvs_wifi_settings));
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_commit(handle);
		if (esp_err != ESP_OK) return esp_err;

		nvs_close(handle);
#if WIFI_MANAGER_DEBUG
		//ESP_LOGI(TAG, "Write config: ssid:%s password:%s ",wifi_manager_config_sta->sta.ssid,wifi_manager_config_sta->sta.password);
		//ESP_LOGI(TAG, "Write config: deepsheep_time: 0x%08x ",nvs_wifi_settings.deepsheep_time);
		//ESP_LOGI(TAG, "Write config: sta_ip_addr: %s ", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.ip));
#if 0	
		
		//ESP_LOGI(TAG, "Write config: SoftAP_pwd: %s ",nvs_wifi_settings.ap_pwd);
		//ESP_LOGI(TAG, "Write config:: SoftAP_hidden (1 = yes): %i",nvs_wifi_settings.ap_ssid_hidden);
		//ESP_LOGI(TAG, "Write config: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz): %i",nvs_wifi_settings.ap_bandwidth);
		//ESP_LOGI(TAG, "Write config: boot_normal (0 = reset, 1 = boot_normal when connected): %i",nvs_wifi_settings.boot_normal);
		//ESP_LOGI(TAG, "Write config: sta_power_save (1 = yes): %i",nvs_wifi_settings.sta_power_save);
		//ESP_LOGI(TAG, "Write config: sta_static_ip (0 = dhcp client, 1 = static ip): %i",nvs_wifi_settings.sta_static_ip);
		//ESP_LOGI(TAG, "Write config: sta_ip_addr: %s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.ip));
		//ESP_LOGI(TAG, "Write config:: sta_gw_addr: %s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.gw));
		//ESP_LOGI(TAG, "Write config: sta_netmask: %s", ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.netmask));
#endif

#endif
	}

	return ESP_OK;
}
// END NVS
/////////////////////////////////////////////////
// START WIFI MANAGER

static void wifi_init_sta() 
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {.ssid = EXAMPLE_ESP_WIFI_SSID, .password = EXAMPLE_ESP_WIFI_PASS},
    };
	
	ESP_LOGI(TAG, "wifi_init_sta ssid.");
	memcpy(wifi_config.sta.ssid, wifi_manager_config_sta->sta.ssid , sizeof(wifi_manager_config_sta->sta.ssid));
	memcpy(wifi_config.sta.password, wifi_manager_config_sta->sta.password, sizeof(wifi_manager_config_sta->sta.password));
	
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", wifi_config.sta.ssid,
            wifi_config.sta.password);
    
    //xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);	//48.7day
	uxBits =xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, xDelay);
	if( ( uxBits & ( BIT_0 | BIT_4 ) ) == ( BIT_0 | BIT_4 ) )
	  ESP_LOGI(TAG, "returned because both bits were set.");
	else if( ( uxBits & BIT_0 ) != 0 )
	  ESP_LOGI(TAG, "returned because just BIT_0 was set.");
	else if( ( uxBits & BIT_4 ) != 0 )
	  ESP_LOGI(TAG, "returned because just BIT_4 was set.");
	else
	  ESP_LOGI(TAG, "returned because xTicksToWait ticks passed without either BIT_0 or BIT_4 becoming set");

}


void wifi_boot_config(){

	/* memory allocation of objects used by the task */
	wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
	memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
	memset(&nvs_wifi_settings.server_ip_config, 0x00, sizeof(tcpip_adapter_ip_info_t));
	IP4_ADDR(&nvs_wifi_settings.server_ip_config.gw, 192, 168, 100, 1);
	IP4_ADDR(&nvs_wifi_settings.server_ip_config.netmask, 255, 255, 255, 0);
	
	/* try to get access to previously saved wifi */
	if(wifi_manager_read_nvs_wifi_config()){
		printf("wifi_manager: saved wifi found on startup\n");
		wifi_mode = 1;		
	}
	else{
		printf("wifi_manager: saved wifi not found on startup\n");
		wifi_mode = 0;
	}
}

void wifi_manager_start(){

	if(wifi_mode == 1){
		printf("wifi_manager: start sta mode\n");
		wifi_init_sta();
	}
	else{
		printf("wifi_manager: start smart config \n");
		//SMART.S
		ESP_LOGI(TAG, "START SMART_CONFIG...!!");
		smartconfig_wifi_start();
		ESP_LOGI(TAG, "END SMART_CONFIG...!!");
		//SMART.E
	}
}

void nvs_start_init()
{
	/* initialize flash memory */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ESP_ERROR_CHECK( nvs_flash_init() );
    }
    ESP_ERROR_CHECK(gpio_install_isr_service(0));	
	gpio_set_direction(CAMERA_LED_GPIO, GPIO_MODE_OUTPUT);
}
void camera_start_init()
{
    esp_err_t err;
 
    camera_config_t camera_config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = CONFIG_D0,
        .pin_d1 = CONFIG_D1,
        .pin_d2 = CONFIG_D2,
        .pin_d3 = CONFIG_D3,
        .pin_d4 = CONFIG_D4,
        .pin_d5 = CONFIG_D5,
        .pin_d6 = CONFIG_D6,
        .pin_d7 = CONFIG_D7,
        .pin_xclk = CONFIG_XCLK,
        .pin_pclk = CONFIG_PCLK,
        .pin_vsync = CONFIG_VSYNC,
        .pin_href = CONFIG_HREF,
        .pin_sscb_sda = CONFIG_SDA,
        .pin_sscb_scl = CONFIG_SCL,
        .pin_reset = CONFIG_RESET,
        .xclk_freq_hz = CONFIG_XCLK_FREQ,
    };

    camera_model_t camera_model;
    err = camera_probe(&camera_config, &camera_model);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x", err);
        return;
    }

    if (camera_model == CAMERA_OV7725) {
        s_pixel_format = CAMERA_PIXEL_FORMAT;
        camera_config.frame_size = CAMERA_FRAME_SIZE;
        ESP_LOGI(TAG, "Detected OV7725 camera, using %s bitmap format",
                CAMERA_PIXEL_FORMAT == CAMERA_PF_GRAYSCALE ?
                        "grayscale" : "RGB565");
    } else if (camera_model == CAMERA_OV2640) {
        ESP_LOGI(TAG, "Detected OV2640 camera, using JPEG format");
        s_pixel_format = CAMERA_PF_JPEG;
        camera_config.frame_size = CAMERA_FRAME_SIZE;
        camera_config.jpeg_quality = 15; //15;
    } else {
        ESP_LOGE(TAG, "Camera not supported");
        return;
    }

    camera_config.pixel_format = s_pixel_format;
    err = camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }	
}

void deepsleep_start()
{
    ESP_LOGI(TAG,"==START DEEP SLEEP ==");	
	int resion = esp_sleep_get_wakeup_cause();
	printf("Wake up reaion = %d \n",resion);
	//DEEP.S	
	switch (resion) {

		case ESP_SLEEP_WAKEUP_EXT0: {
            printf("[DEEP] 2.signal using RTC_IO \n");
            break;
        }
		case ESP_SLEEP_WAKEUP_EXT1: {
            printf("[DEEP] 3.signal using RTC_CNTL\n");
            break;
        }
		case ESP_SLEEP_WAKEUP_TIMER: {
            printf("[DEEP] 4.Wake up from timer\n");
            break;
        }
		case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            printf("Not a deep sleep reset %d\n", resion);
    }	
	uint64_t wakeup_time_sec = nvs_wifi_settings.deepsheep_time * 1000000;
    ESP_LOGI(TAG,"Enabling timer wakeup, %lld sec", wakeup_time_sec/1000000);
    esp_sleep_enable_timer_wakeup(wakeup_time_sec);	
	ESP_LOGI(TAG,"Entering deep sleep");
	
#if 1

    camera_deinit();
	/*
	for(int i = 0; i < 36 ;i++)
	{
		if(gpio_get_level(i))
			gpio_set_level(i, 0);		
	}
	*/

	gpio_set_level(CONFIG_RESET, 0);  //TOMATO 10mA
    
	esp_wifi_stop();
	rtc_gpio_isolate(GPIO_NUM_12);
#endif	

#if 0		

	//esp_bluedroid_disable();
	//esp_bluedroid_deinit();
	//esp_bt_controller_disable();
	//esp_bt_controller_deinit();
	//esp_bt_mem_release(ESP_BT_MODE_BTDM);
		
	REG_SET_FIELD(RTC_CNTL_REG, RTC_CNTL_DBIAS_WAK, 4);
    REG_SET_FIELD(RTC_CNTL_REG, RTC_CNTL_DBIAS_SLP, 4);
	esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
	esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
	esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
	esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
#endif	
	//esp_light_sleep_start();
	
	struct timeval live_now;
    gettimeofday(&live_now, NULL);
	int sleep_time = live_now.tv_sec - sleep_enter_time.tv_sec;
	ESP_LOGI(TAG, "== End App! ==  %d s",sleep_time);
	
	esp_deep_sleep_start();
	//DEEP.E
}
/*Client mode support work in progress
*/

void LED_TaskDelay(int param){
	
	gpio_set_level(CAMERA_LED_GPIO, 1);
	vTaskDelay( 200 / portTICK_PERIOD_MS);
	gpio_set_level(CAMERA_LED_GPIO, 0);
	vTaskDelay( 200 / portTICK_PERIOD_MS);
	gpio_set_level(CAMERA_LED_GPIO, 1);
	vTaskDelay( 200 / portTICK_PERIOD_MS);
	gpio_set_level(CAMERA_LED_GPIO, 0);
	param = param - 600;
	vTaskDelay( param / portTICK_PERIOD_MS);	
	retry_count++;
	gpio_set_level(CAMERA_LED_GPIO, 0);
	
}
static void http_client(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s;
	// int r;
    // char recv_buf[64];
	char* request_header_str;
	
	
	char port_buf[6]; 
	sprintf(port_buf,"%d",nvs_wifi_settings.server_port); 
	
	retry_count = 0; 
	

    while(1) {
        /* Wait for the callback to set the CONNECTED_BIT in the
           event group.
        */
		
        //xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);	//48.7day
		xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, xDelay);
		
		if(retry_count >10)  //MAX_RETRY
		{
			ESP_LOGE(TAG, "HTTP Sever MAX_RETRY !");
			break;
		}							
 
		int err = getaddrinfo(ip4addr_ntoa(&nvs_wifi_settings.server_ip_config.ip), port_buf, &hints, &res);
       // int err = getaddrinfo(ip4addr_ntoa(WEB_SERVER, "80", &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            LED_TaskDelay(1000);
            continue;
        }

        /* Code to print the resolved IP.
           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            LED_TaskDelay(1000);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d retry_cnt=%d", errno, retry_count);
            close(s);
            freeaddrinfo(res);
            LED_TaskDelay(1000);
            continue;
        }
        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);		
		
		//Once connected to the server , capture the picture
		err_t err1 =camera_run();
		if (err1 != ESP_OK)
		{
            ESP_LOGI(TAG, "Camera capture failed with error = %d", err1);
			retry_count++;
        }
		else {
            ESP_LOGI(TAG, "Done");
			int image_size = camera_get_data_size();
			asprintf(&request_header_str, "%s%s %d%s%s%s",
					REQUEST_POST,
					REQUEST_LEN,image_size,REQUEST_END,
					REQUEST_TYPE,REQUEST_END);
            //Send jpeg	
			if (write(s, request_header_str, strlen(request_header_str)) < 0) {			
			//if (write(s, REQUEST, strlen(REQUEST)) < 0) {
				ESP_LOGE(TAG, "1... socket send failed");
				close(s);
				LED_TaskDelay(1000);
				continue;
			}
			ESP_LOGI(TAG, "size=%d, %d %d ", image_size, strlen(REQUEST),strlen(request_header_str));
			
			if (write(s,camera_get_fb(),image_size) < 0) {
				ESP_LOGE(TAG, "2... socket send failed");
				close(s);
				LED_TaskDelay(4000);
				continue;
			}			
			ESP_LOGI(TAG, "... socket send success");   
        }
		close(s);
		vTaskDelay(2500 / portTICK_PERIOD_MS); //TasK BUG
		break;
#if 0
        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);
        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
        close(s);
#endif		
    }
	deepsleep_start();
}

static void run_time(void *pvParameters)
{
	ESP_LOGI(TAG, "== run_time ==  start ");
	gpio_set_level(CAMERA_LED_GPIO, 1);
	  
	struct timeval first_now;
	struct timeval live_now;
	int sleep_time = 0;
    gettimeofday(&first_now, NULL);
	
	if(wifi_mode == 1){
		nvs_wifi_settings.boot_normal = 0;
		wifi_manager_save_nvs_wifi_config();
	}	
	
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	
	if(wifi_mode == 1){
		nvs_wifi_settings.boot_normal = 1;
		wifi_manager_save_nvs_wifi_config();
		gpio_set_level(CAMERA_LED_GPIO, 0);
	}	 

    gettimeofday(&live_now, NULL);
	sleep_time = live_now.tv_sec - first_now.tv_sec;
	ESP_LOGI(TAG, "== run_time ==  %d s",sleep_time);		

    vTaskDelete(NULL);
	
}

void InitVal()
{
	int i = 0;
	for( i =0 ; i < MAX_TEMP_FIELD_SIZE ; i++){
		nvs_wifi_settings.temp_field[i] = 0;		
	}	
}
void app_main()
{
	
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("gpio", ESP_LOG_WARN);
	ESP_LOGI(TAG, "== Starting App! == ");
	InitVal();
	nvs_start_init();
	gettimeofday(&sleep_enter_time, NULL);
   
	wifi_boot_config(); 
	if(wifi_mode == 1){	
		camera_start_init();
	}
	wifi_manager_start();
	xTaskCreate(&run_time, "run_time", 2048, NULL, 5, NULL);	
	if(wifi_mode == 1)
		xTaskCreate(&http_client, "http_client", 2048, NULL, 5, NULL);	
	
}
