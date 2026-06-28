/**
 * Author: Julia Scott (Julia-Scott-161)
 */

#include "gamepad.h"
static const char *TAG = "gamepad";
static accel_t accel_device;

static volatile bool     connection = false;
static SemaphoreHandle_t mutex     = NULL;
static TaskHandle_t      handle_task  = NULL;
static uint8_t           buffer[GAMEPAD_REPORT_SIZE] = {0};


void gamepad_set_connected(bool status)
{
    connection = status;
}

void send_gamepad_report(uint16_t buttons)
{
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    buffer[0] = buttons & 0xFF; //buttons 1-8
    buffer[1] = (buttons >> 8) & 0xFF; //buttons 9-11
    esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, GAMEPAD_REPORT_ID,
                                  GAMEPAD_REPORT_SIZE, buffer);
    xSemaphoreGive(mutex);
}

// ---------- Tasks ---------- //

static void led_task(void *pvParameters)
{
    while (true) {
        if (connection) {
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        else {
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(250 / portTICK_PERIOD_MS);
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(250 / portTICK_PERIOD_MS);
        }
    }
}

static void gamepad_task(void *pvParameters)
{
    ESP_LOGI(TAG, "gamepad task started");
    uint16_t last_buttons = 0xFFFF;
    int tick = 0;

    while (true) {
        // --- Buttons ---
        uint8_t button_up    = !gpio_get_level(BUTTON_UP);
        uint8_t button_down  = !gpio_get_level(BUTTON_DOWN);
        uint8_t button_left  = !gpio_get_level(BUTTON_LEFT);
        uint8_t button_right = !gpio_get_level(BUTTON_RIGHT);

        uint8_t button_a     = !gpio_get_level(BUTTON_A);
        uint8_t button_b     = !gpio_get_level(BUTTON_B);

        uint8_t button_plus  = !gpio_get_level(BUTTON_PLUS);
        uint8_t button_minus = !gpio_get_level(BUTTON_MINUS);
        uint8_t button_home  = !gpio_get_level(BUTTON_HOME);

        uint8_t button_1     = !gpio_get_level(BUTTON_1);
        uint8_t button_2     = !gpio_get_level(BUTTON_2);


        uint16_t buttons =
                //((uint16_t)button_up << 0)
                (button_up    << 0)  |
                (button_down  << 1)  |
                (button_left  << 2)  |
                (button_right << 3)  |
                (button_a     << 4)  |
                (button_b     << 5)  |
                (button_minus << 6)  |
                (button_home  << 7)  |
                (button_plus  << 8)  |
                (button_1     << 9)  |
                (button_2     << 10);

        //For testing button outputs from monitor's end
        if (buttons != last_buttons) {
            ESP_LOGI(TAG, "GPIO raw: UP=%d DN=%d LT=%d RT=%d A=%d B=%d -=%d HOME=%d +=%d 1=%d 2=%d",
                     button_up, button_down, button_left, button_right,
                     button_a, button_b, button_minus, button_home,
                     button_plus, button_1, button_2);
            ESP_LOGI(TAG, "Button mask: 0x%04X  buf[0]=0x%02X buf[1]=0x%02X",
                     buttons, (uint8_t)(buttons & 0xFF), (uint8_t)((buttons >> 8) & 0xFF));
            last_buttons = buttons;
        }
// ----------- Acceleration ---------- //
        accel_g_t accel = {0};
        esp_err_t accel_err = read_g_values(&accel_device, &accel);

        // converts the read g values to wiimote 10-bit range
        uint16_t accel_x, accel_y, accel_z;
        if (accel_err == ESP_OK) {
            accel_x = (uint16_t)((accel.x * 170.0f) + 512);
            accel_y = (uint16_t)((accel.y * 170.0f) + 512);
            accel_z = (uint16_t)((accel.z * 170.0f) + 512);
            accel_x = accel_x > 1023 ? 1023 : accel_x;
            accel_y = accel_y > 1023 ? 1023 : accel_y;
            accel_z = accel_z > 1023 ? 1023 : accel_z;
        } else {
            accel_x = accel_y = accel_z = 0; // neutral on failure
        }

// ---------- Testing from monitor end ---------- //
        if (ACCEL_DEBUG_LOG) {
            if (++tick % 20 == 0) {
                ESP_LOGI(TAG, "ACCEL X:%4u Y:%4u Z:%4u",
                         accel_x, accel_y, accel_z);
            }
        }

        if (xSemaphoreTake(mutex, portMAX_DELAY)) {
            buffer[0] = buttons & 0xFF;
            buffer[1] = (buttons >> 8) & 0xFF;
            esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, GAMEPAD_REPORT_ID,
                                          GAMEPAD_REPORT_SIZE, buffer);
            xSemaphoreGive(mutex);
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
/**
 * bus_config
 */
static i2c_master_bus_handle_t i2c_init() {
    i2c_master_bus_config_t bus_config = {
            .i2c_port         = I2C_NUM_0,
            .sda_io_num       = I2C_SDA,
            .scl_io_num       = I2C_SCL,
            .clk_source       = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    ESP_LOGI(TAG, "MPU-6050 initialized");
    return bus_handle;
}

void gamepad_init()
{
    const int btn_pins[] = {
            BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT,
            BUTTON_A, BUTTON_B,
            BUTTON_MINUS, BUTTON_HOME, BUTTON_PLUS,
            BUTTON_1, BUTTON_2
    };
    int button_size = sizeof(btn_pins) / sizeof(btn_pins[0]);

    for (int i = 0; i < button_size; i++) {
        gpio_set_direction(btn_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(btn_pins[i], GPIO_PULLUP_ONLY);
    }
    // Acceleration
    i2c_master_bus_handle_t bus = i2c_init();
    ESP_ERROR_CHECK(accel_init(&accel_device, bus, ACCEL_RANGE_2G));

    // LED
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    xTaskCreate(led_task, "led_task", 1024, NULL, configMAX_PRIORITIES - 3, NULL);

    ESP_LOGI(TAG, "gamepad initialized");
}

void gamepad_task_start()
{
    mutex = xSemaphoreCreateMutex();
    xTaskCreate(gamepad_task, "gamepad_task", 8 * 1024, NULL,
                configMAX_PRIORITIES - 3, &handle_task);
}

void gamepad_task_stop()
{
    if (handle_task) {
        vTaskDelete(handle_task);
        handle_task = NULL;
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = NULL;
    }
}