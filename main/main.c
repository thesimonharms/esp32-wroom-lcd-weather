// (C) 2025 Simon Harms
// @thesimonharms
// MIT Lisence
// Weather Display using ESP32, OpenWeather API and HD44780 I2C LCD

// Include necessary headers for ESP32, FreeRTOS, HTTP client, JSON parsing, and LCD
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "HD44780.h"

// Define constants for LCD and WiFi configuration
#define LCD_ADDR 0x27
#define SDA_PIN  21
#define SCL_PIN  22
#define LCD_COLS 16
#define LCD_ROWS 2

// Change these values before compiling and flashing
#define WIFI_SSID "YOUR SSID"
#define WIFI_PASS "YOUR WIFI PASSWORD"
#define WEATHER_API_KEY "YOUR OPENWEATHER API KEY"
#define LOCATION_LAT 0.0000
#define LOCATION_LON 0.0000
#define PAGE_MS 10000

// Define logging tag and buffer for weather data
static const char *TAG = "weather_lcd";
static char weather_data[2048] = {0};  // Increased buffer

// Currently not needed but could be useful later
// External references to root certificate for HTTPS
// extern const uint8_t root_cert_pem_start[] asm("_binary_root_cert_pem_start");
// extern const uint8_t root_cert_pem_end[] asm("_binary_root_cert_pem_end");

// Event group for WiFi connection status
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAXIMUM_RETRY      5

// Queue for passing weather data between tasks
static QueueHandle_t weather_queue;

// Structure to hold parsed weather data
typedef struct {
    float temp;
    float feels_like;
    float humidity;
    float pressure;
    char weather_main[32];
    char weather_desc[64];
} weather_data_t;

// Initialize LCD with test message
static void lcd_init(void) {
    ESP_LOGI(TAG, "Initializing LCD...");
    LCD_init(LCD_ADDR, SDA_PIN, SCL_PIN, LCD_COLS, LCD_ROWS);
    LCD_clearScreen();
    ESP_LOGI(TAG, "LCD initialization complete");
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
}

// General event handler for WiFi and IP events
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi
static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_any_id));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK  // Explicitly set WPA2
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);
}

// HTTP event handler
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "Connected to server");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG, "Headers sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "Header: %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (strlen(weather_data) + evt->data_len < sizeof(weather_data)) {
                memcpy(weather_data + strlen(weather_data), evt->data, evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "Finish");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected");
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "Error");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "Redirect");
            break;
    }
    return ESP_OK;
}

// Fetch weather data from OpenWeather API
static void get_weather(void) {
    char url[256];
    snprintf(url, sizeof(url), 
        "http://api.openweathermap.org/data/3.0/onecall?"
        "lat=%.4f&lon=%.4f"
        "&exclude=minutely,hourly,daily,alerts"  // Minimize data
        "&units=imperial"
        "&appid=%s",
        LOCATION_LAT, LOCATION_LON, WEATHER_API_KEY);
    
    ESP_LOGI(TAG, "URL: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .timeout_ms = 10000,
        .buffer_size = 2048
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET Status = %d", status);
        ESP_LOGI(TAG, "Response: %s", weather_data);
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
}

// Helper function to scroll text on LCD
static void scroll_text(const char* text, int row, int delay_ms) {
    int len = strlen(text);
    if (len <= LCD_COLS) {
        LCD_setCursor(0, row);
        LCD_writeStr((char*)text);
        return;
    }
    
    char buffer[17] = {0};
    for (int i = 0; i <= len - LCD_COLS; i++) {
        LCD_setCursor(0, row);
        strncpy(buffer, text + i, LCD_COLS);
        buffer[LCD_COLS] = '\0';
        LCD_writeStr(buffer);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// Helper function to convert string to uppercase
static void str_to_upper(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper(str[i]);
    }
}

// Parse weather data from JSON response
static bool parse_weather_data(weather_data_t* data) {
    cJSON *root = cJSON_Parse(weather_data);
    if (!root) return false;
    
    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (!current) {
        cJSON_Delete(root);
        return false;
    }
    
    data->temp = cJSON_GetObjectItem(current, "temp")->valuedouble;
    data->feels_like = cJSON_GetObjectItem(current, "feels_like")->valuedouble;
    data->humidity = cJSON_GetObjectItem(current, "humidity")->valuedouble;
    data->pressure = cJSON_GetObjectItem(current, "pressure")->valuedouble;
    
    cJSON *weather = cJSON_GetArrayItem(cJSON_GetObjectItem(current, "weather"), 0);
    strncpy(data->weather_main, cJSON_GetObjectItem(weather, "main")->valuestring, sizeof(data->weather_main)-1);
    strncpy(data->weather_desc, cJSON_GetObjectItem(weather, "description")->valuestring, sizeof(data->weather_desc)-1);
    
    // Convert to uppercase
    str_to_upper(data->weather_main);
    str_to_upper(data->weather_desc);
    
    cJSON_Delete(root);
    return true;
}

// Task to periodically update weather data
void WeatherUpdateTask(void* param) {
    weather_data_t current_weather = {0};
    
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                             WIFI_CONNECTED_BIT,
                                             pdFALSE,
                                             pdFALSE,
                                             pdMS_TO_TICKS(10000));
        
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGE(TAG, "WiFi not connected");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        memset(weather_data, 0, sizeof(weather_data));
        get_weather();
        
        if (parse_weather_data(&current_weather)) {
            xQueueOverwrite(weather_queue, &current_weather);
        } else {
            ESP_LOGE(TAG, "Failed to parse weather data");
        }
        
        vTaskDelay(pdMS_TO_TICKS(300000)); // 5 minute delay
    }
}

// Task to display weather data on LCD
void DisplayTask(void* param) {
    weather_data_t display_data = {0};
    
    while (1) {
        // Update display data if new data available
        if (xQueueReceive(weather_queue, &display_data, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Updated display data");
        }
        
        // Screen 1: Temperature pair
        LCD_clearScreen();
        char line[17];
        snprintf(line, sizeof(line), "Temp: %.1fF", display_data.temp);
        LCD_writeStr(line);
        LCD_setCursor(0, 1);
        snprintf(line, sizeof(line), "Fls like: %.1fF", display_data.feels_like);
        LCD_writeStr(line);
        vTaskDelay(pdMS_TO_TICKS(PAGE_MS));
        
        // Screen 2: Humidity and Pressure
        LCD_clearScreen();
        snprintf(line, sizeof(line), "Humid: %.0f%%", display_data.humidity);
        LCD_writeStr(line);
        LCD_setCursor(0, 1);
        snprintf(line, sizeof(line), "Press: %.0fhPa", display_data.pressure);
        LCD_writeStr(line);
        vTaskDelay(pdMS_TO_TICKS(PAGE_MS));
        
        // Screen 3: Weather with scrolling description
        LCD_clearScreen();
        LCD_writeStr(display_data.weather_main);
        LCD_setCursor(0, 1);
        scroll_text(display_data.weather_desc, 1, 300);
        vTaskDelay(pdMS_TO_TICKS(PAGE_MS));
    }
}

// Main application entry point
void app_main(void) {
    ESP_LOGI(TAG, "Starting Weather Display");
    lcd_init();

    LCD_writeStr("ESP32 Weather");
    LCD_setCursor(0, 1);
    LCD_writeStr("@thesimonharms");

    vTaskDelay(pdMS_TO_TICKS(8000));
    
    // Clear LCD before continuing
    LCD_clearScreen();
    
    // Create queue for weather data
    weather_queue = xQueueCreate(1, sizeof(weather_data_t));
    
    // Continue with WiFi init and task creation
    wifi_init();
    xTaskCreate(WeatherUpdateTask, "Weather Update", 4096, NULL, 5, NULL);
    xTaskCreate(DisplayTask, "Display", 4096, NULL, 4, NULL);
}