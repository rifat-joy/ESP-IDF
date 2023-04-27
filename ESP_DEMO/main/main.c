#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_pm.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "MQTTS";

esp_mqtt_client_handle_t client;

extern const uint8_t mqtt_eclipseprojects_io_pem_start[]   asm("_binary_mqtt_eclipseprojects_io_pem_start");
extern const uint8_t mqtt_eclipseprojects_io_pem_end[]   asm("_binary_mqtt_eclipseprojects_io_pem_end");

static void mqtt_app_start(void);

// set the CPU frequency to 240 MHz
esp_err_t set_cpu_frequency(void)
{
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = true
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    return ret;
}

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
    	esp_wifi_connect();
        printf("WiFi connecting ... \n");
        break;

    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
//        printf("WiFi lost connection, trying to reconnect... \n");
    	printf(".");
        esp_wifi_connect();
        break;

	case IP_EVENT_STA_GOT_IP:
		printf("WiFi got IP: starting MQTT Client\n");
		mqtt_app_start();
		break;

    default:
        break;
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        cJSON *json = cJSON_Parse(event->data);
        if (json == NULL) {
            printf("Error parsing JSON string: %s\n", cJSON_GetErrorPtr());
        } else {
            cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "Item");
            cJSON *quantity = cJSON_GetObjectItemCaseSensitive(json, "Qty");
            cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "Type");
            printf("Item: %s, Qty: %d , Type: %s\n", item->valuestring, quantity->valueint, type->valuestring);
            cJSON_Delete(json);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/* Initialize Wi-Fi as sta and set scan method */
static void connect_to_wifi(void)
{
	// 1 - Wi-Fi/LwIP Init Phase
	esp_netif_init();										// TCP/IP initiation -> s1.1
    esp_event_loop_create_default();						// event loop 		 -> s1.2
    esp_netif_create_default_wifi_sta();					// WiFi station 	 -> s1.3
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "VerticalInv",
            .password = "@VIL@123",
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
			.pmf_cfg = {
				.capable = true,
				.required = false
			}
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    // 3 - Wi-Fi Start Phase
    if(esp_wifi_start() == ESP_OK){
    	printf("wifi started.!\n");
    }

    // 4- Wi-Fi Connect Phase
//    if(esp_wifi_connect() == ESP_OK){
//    	printf("wifi connected.!");
//    }
}

static void mqtt_app_start(void)
{
  const esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = "mqtts://prohorii.vertical-innovations.com",
    .broker.verification.certificate = (const char *)mqtt_eclipseprojects_io_pem_start,
	.broker.address.port = (uint32_t)8883,
    .credentials.username = (const char *)"vilmqtt",
	.credentials.authentication.password = (const char *)"mvqitlt",
  };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void Publisher_Task(void *params)
{
	// Create a JSON object with the "name" and "age" fields
	cJSON *json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "name", "Rifat Islam");
	cJSON_AddNumberToObject(json, "age", 27);

	// Print the JSON document to a string
	char *json_str = cJSON_Print(json);
	while (true)
	  {
		if(MQTT_EVENT_CONNECTED)
		{
			esp_mqtt_client_publish(client, "/topic/test3", json_str, strlen(json_str), 0, 0);
//			cJSON_free(json_str);
			// Delete the JSON object to free the memory
			cJSON_Delete(json);
			vTaskDelay(5000 / portTICK_PERIOD_MS);
		}
	  }
}

void app_main(void){
//	set_cpu_frequency();
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK( ret );

	connect_to_wifi();

	vTaskDelay(2000 / portTICK_PERIOD_MS);
//	xTaskCreate(Publisher_Task, "Publisher_Task", 1024 * 5, NULL, 5, NULL);
}
