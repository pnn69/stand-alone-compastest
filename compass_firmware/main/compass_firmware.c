#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/i2c_master.h"
#include "led_strip.h"
#include "esp_http_server.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Configuration
#define WIFI_SSID      "NicE_WiFi"
#define WIFI_PASS      "!Ni1001100110"
#define MAX_STA_CONN   4

#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_NUM              0
#define I2C_MASTER_FREQ_HZ          100000

#define ICM20948_ADDR_1             0x68
#define ICM20948_ADDR_2             0x69
#define AK09916_ADDR                0x0C

#define LED_GPIO                    32
#define LED_NUM                     3

static const char *TAG = "CompassTest";

// Data Structure
typedef struct {
    float magX, magY, magZ; // Compensated values
    float minX, minY, minZ; // Raw mins for calibration
    float maxX, maxY, maxZ; // Raw maxes for calibration
    float heading;
    bool sensor_ok;
    
    // Calibration State
    bool is_calibrating;
    float cal_offset[3];
    float cal_matrix[3][3];
} compass_data_t;

static compass_data_t g_data = {
    .minX = 10000, .minY = 10000, .minZ = 10000,
    .maxX = -10000, .maxY = -10000, .maxZ = -10000,
    .sensor_ok = false,
    .is_calibrating = false,
    .cal_offset = {0, 0, 0},
    .cal_matrix = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    }
};
static SemaphoreHandle_t g_data_mux;

// Handles
i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t icm_handle;
i2c_master_dev_handle_t mag_handle;
led_strip_handle_t led_strip;

// Function Prototypes
void wifi_init_sta(void);
void i2c_master_init(void);
void icm20948_init(void);
void led_strip_init(void);
esp_err_t start_rest_server(void);
void load_calibration(void);
void save_calibration(void);

// NVS Calibration Functions
void load_calibration(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS storage found, using default identity calibration");
        return;
    }
    
    size_t len = sizeof(g_data.cal_offset);
    if (nvs_get_blob(my_handle, "cal_offset", g_data.cal_offset, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded calibration offset");
    }
    
    len = sizeof(g_data.cal_matrix);
    if (nvs_get_blob(my_handle, "cal_matrix", g_data.cal_matrix, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded calibration matrix");
    }
    
    nvs_close(my_handle);
}

void save_calibration(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return;
    
    nvs_set_blob(my_handle, "cal_offset", g_data.cal_offset, sizeof(g_data.cal_offset));
    nvs_set_blob(my_handle, "cal_matrix", g_data.cal_matrix, sizeof(g_data.cal_matrix));
    nvs_commit(my_handle);
    nvs_close(my_handle);
    ESP_LOGI(TAG, "Calibration saved to NVS");
}

// WiFi Event Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying connection to AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi Station Started. Connecting to SSID:%s", WIFI_SSID);
}

void i2c_master_init(void) {
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    ESP_LOGI(TAG, "Scanning I2C bus...");
    uint8_t found_addr = 0;
    for (int i = 1; i < 127; i++) {
        if (i2c_master_probe(bus_handle, i, 100) == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", i);
            if (i == ICM20948_ADDR_1 || i == ICM20948_ADDR_2) found_addr = i;
        }
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = found_addr ? found_addr : ICM20948_ADDR_1,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &icm_handle));

    dev_cfg.device_address = AK09916_ADDR;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &mag_handle));
}

void icm20948_init(void) {
    uint8_t data[2];
    data[0] = 0x7F; data[1] = 0x00; // Bank 0
    if (i2c_master_transmit(icm_handle, data, 2, -1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to communicate with ICM20948");
        return;
    }
    
    // Reset
    data[0] = 0x06; data[1] = 0x81;
    i2c_master_transmit(icm_handle, data, 2, -1);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    data[0] = 0x06; data[1] = 0x01; // Wake up
    i2c_master_transmit(icm_handle, data, 2, -1);
    
    data[0] = 0x0F; data[1] = 0x02; // Bypass enable
    i2c_master_transmit(icm_handle, data, 2, -1);

    uint8_t who_am_i_reg = 0x01; // AK09916 WIA2
    uint8_t who_am_i_val = 0;
    if (i2c_master_transmit_receive(mag_handle, &who_am_i_reg, 1, &who_am_i_val, 1, -1) == ESP_OK) {
        ESP_LOGI(TAG, "AK09916 WHO_AM_I: 0x%02X", who_am_i_val);
        g_data.sensor_ok = (who_am_i_val == 0x09 || who_am_i_val == 0x48);
    } else {
        ESP_LOGE(TAG, "Failed to communicate with AK09916");
    }

    data[0] = 0x31; data[1] = 0x08; // Continuous mode 4 (100Hz)
    i2c_master_transmit(mag_handle, data, 2, -1);
}

void led_strip_init(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_NUM,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

// Sensor Task
void sensor_task(void *pvParameters) {
    uint8_t mag_reg = 0x11; // HXL
    uint8_t mag_data[8]; // HXL, HXH, HYL, HYH, HZL, HZH, TMPS, ST2
    int log_cnt = 0;
    
    while (1) {
        if (i2c_master_transmit_receive(mag_handle, &mag_reg, 1, mag_data, 8, -1) == ESP_OK) {
            int16_t x_raw = (int16_t)((mag_data[1] << 8) | mag_data[0]);
            int16_t y_raw = (int16_t)((mag_data[3] << 8) | mag_data[2]);
            int16_t z_raw = (int16_t)((mag_data[5] << 8) | mag_data[4]);

            float rx = x_raw * 0.15f; 
            float ry = y_raw * 0.15f;
            float rz = z_raw * 0.15f;

            xSemaphoreTake(g_data_mux, portMAX_DELAY);
            
            if (g_data.is_calibrating) {
                if (rx < g_data.minX) g_data.minX = rx;
                if (ry < g_data.minY) g_data.minY = ry;
                if (rz < g_data.minZ) g_data.minZ = rz;
                if (rx > g_data.maxX) g_data.maxX = rx;
                if (ry > g_data.maxY) g_data.maxY = ry;
                if (rz > g_data.maxZ) g_data.maxZ = rz;
            }

            // Apply Hard Iron Offset
            float hx = rx - g_data.cal_offset[0];
            float hy = ry - g_data.cal_offset[1];
            float hz = rz - g_data.cal_offset[2];

            // Apply Soft Iron Matrix
            g_data.magX = hx * g_data.cal_matrix[0][0] + hy * g_data.cal_matrix[0][1] + hz * g_data.cal_matrix[0][2];
            g_data.magY = hx * g_data.cal_matrix[1][0] + hy * g_data.cal_matrix[1][1] + hz * g_data.cal_matrix[1][2];
            g_data.magZ = hx * g_data.cal_matrix[2][0] + hy * g_data.cal_matrix[2][1] + hz * g_data.cal_matrix[2][2];

            // Calculate heading (Standard compass: 0=N, 90=E, 180=S, 270=W)
            g_data.heading = atan2f(-g_data.magY, g_data.magX) * 180.0f / (float)M_PI;
            if (g_data.heading < 0) g_data.heading += 360.0f;
            
            if (++log_cnt >= 10) {
                ESP_LOGD(TAG, "Heading: %.2f, Mag: [%.2f, %.2f, %.2f]", g_data.heading, g_data.magX, g_data.magY, g_data.magZ);
                log_cnt = 0;
            }
            xSemaphoreGive(g_data_mux);

            // LED Update
            if (g_data.sensor_ok) {
                if (g_data.is_calibrating) {
                    led_strip_set_pixel(led_strip, 0, 255, 255, 0); // Yellow while calibrating
                } else {
                    led_strip_set_pixel(led_strip, 0, 0, 255, 0); // LED 0 Green
                }
            } else {
                led_strip_set_pixel(led_strip, 0, 255, 0, 0); // LED 0 Red
            }

            if (fabsf(g_data.heading - 180.0f) <= 10.0f) {
                led_strip_set_pixel(led_strip, 1, 0, 255, 0); // LED 1 Green (South)
            } else {
                led_strip_set_pixel(led_strip, 1, 0, 0, 0); // Off
            }
            led_strip_refresh(led_strip);
        } else {
            ESP_LOGE(TAG, "Mag Read Failed");
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz
    }
}

// Web Server Implementation
static esp_err_t root_get_handler(httpd_req_t *req) {
    extern const char index_html_start[] asm("_binary_index_html_start");
    extern const char index_html_end[]   asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_size);
    return ESP_OK;
}

static esp_err_t data_get_handler(httpd_req_t *req) {
    char json_str[512];
    xSemaphoreTake(g_data_mux, portMAX_DELAY);
    snprintf(json_str, sizeof(json_str), 
        "{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"minX\":%.2f,\"minY\":%.2f,\"minZ\":%.2f,\"maxX\":%.2f,\"maxY\":%.2f,\"maxZ\":%.2f,\"heading\":%.2f,\"ok\":%s,\"is_calibrating\":%s}",
        g_data.magX, g_data.magY, g_data.magZ, 
        g_data.minX, g_data.minY, g_data.minZ,
        g_data.maxX, g_data.maxY, g_data.maxZ,
        g_data.heading, g_data.sensor_ok ? "true" : "false",
        g_data.is_calibrating ? "true" : "false");
    xSemaphoreGive(g_data_mux);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    return ESP_OK;
}

static esp_err_t calibrate_post_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char cmd[10];
        if (httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd)) == ESP_OK) {
            xSemaphoreTake(g_data_mux, portMAX_DELAY);
            if (strcmp(cmd, "start") == 0) {
                g_data.is_calibrating = true;
                g_data.minX = 10000; g_data.minY = 10000; g_data.minZ = 10000;
                g_data.maxX = -10000; g_data.maxY = -10000; g_data.maxZ = -10000;
                ESP_LOGI(TAG, "Calibration started");
            } else if (strcmp(cmd, "stop") == 0) {
                g_data.is_calibrating = false;
                
                // Calculate Hard Iron Offset
                float offset_x = (g_data.maxX + g_data.minX) / 2.0f;
                float offset_y = (g_data.maxY + g_data.minY) / 2.0f;
                float offset_z = (g_data.maxZ + g_data.minZ) / 2.0f;
                
                // Calculate Soft Iron Diagonal Scale Matrix
                float delta_x = (g_data.maxX - g_data.minX) / 2.0f;
                float delta_y = (g_data.maxY - g_data.minY) / 2.0f;
                float delta_z = (g_data.maxZ - g_data.minZ) / 2.0f;
                
                // Prevent divide by zero
                if(delta_x == 0) delta_x = 1;
                if(delta_y == 0) delta_y = 1;
                if(delta_z == 0) delta_z = 1;

                float avg_delta = (delta_x + delta_y + delta_z) / 3.0f;

                g_data.cal_offset[0] = offset_x;
                g_data.cal_offset[1] = offset_y;
                g_data.cal_offset[2] = offset_z;

                memset(g_data.cal_matrix, 0, sizeof(g_data.cal_matrix));
                g_data.cal_matrix[0][0] = avg_delta / delta_x;
                g_data.cal_matrix[1][1] = avg_delta / delta_y;
                g_data.cal_matrix[2][2] = avg_delta / delta_z;

                ESP_LOGI(TAG, "Calibration stopped. Offsets: %.2f, %.2f, %.2f", offset_x, offset_y, offset_z);
                xSemaphoreGive(g_data_mux);
                
                save_calibration();
                
                httpd_resp_sendstr(req, "Calibration stopped and saved.");
                return ESP_OK;
            }
            xSemaphoreGive(g_data_mux);
        }
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
static const httpd_uri_t data_api = { .uri = "/api/data", .method = HTTP_GET, .handler = data_get_handler };
static const httpd_uri_t cal_api = { .uri = "/api/calibrate", .method = HTTP_POST, .handler = calibrate_post_handler };

esp_err_t start_rest_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &data_api);
        httpd_register_uri_handler(server, &cal_api);
        return ESP_OK;
    }
    return ESP_FAIL;
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_data_mux = xSemaphoreCreateMutex();
    
    load_calibration();
    
    wifi_init_sta();
    i2c_master_init();
    icm20948_init();
    led_strip_init();
    
    start_rest_server();
    
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}
