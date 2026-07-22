/**
 * Written in parallel with gamepad.rs in the RustRemote project
 * RustRemote: (https://github.com/Julia-Scott-161/RustRemote.git)
 *
 * Functions as the input side of CMote: The button GPIOs, the MPU-6050
 * accelerometer, the connection-statis LED, and building/sending Bluetooth
 * HID reports.
 *
 * Two FreeRTOS task live here:
 * 1: LED_task - started once at gamepad_init() and left running for the
    *  program's lifetime.
 * 2: gamepad_task - the polling loop, started/stopped based on Bluetooth
    * connection through gamepad_task_start/gamepad_task_stop so that
    * it doesn't send reports into the void while disconnected.
 *
 * "buffer" holds the most recent report and is guarded by "mutex" so
 * gamepad_task (the writer), and send_gamepad_last_report (reader) can't
 * race.
 *
 * Author: Julia Scott (Julia-Scott-161)
 */

#include "gamepad.h"
static const char *TAG = "gamepad"; //for ESP_LOG
static accel_t accel_device;

static volatile bool     connection = false;
static SemaphoreHandle_t mutex     = NULL;
static TaskHandle_t      handle_task  = NULL;
static uint8_t           buffer[GAMEPAD_REPORT_SIZE] = {0};

// Real Wiimotes default to Core Buttons only (0x30) and switch to a
// different mode when a game requests it through output mode 0x12.
static volatile uint8_t drm_mode = 0x30;
static uint8_t last_report_id  = 0x30;
static uint8_t last_report_len = 0x30;

static uint8_t led_state = 0;
static bool rumble_state = false;

// ---------- Connection State ---------- //
/**
 * flips the status of a "connection" boolean to true or false
 * based on if the esp has successfully been connected to a host.
 * Mainly used for the LED task.
 * @param status true or false based of connection status
 */
void gamepad_set_connected(bool status)
{
    connection = status;
}

/**
 * Called from main.c when output report 0x12 (DRM select) arrives.
 * switches the active reporting mode for 0x30 (core buttons) and
 * 0x31 (core buttons + accel) - which are the two modes gamepad_task
 * and send_gamepad_report know how to build.
 * @param mode the byte requested by the host.
 */
void gamepad_set_drm_mode(uint8_t mode) {
    switch (mode) {
        case 0x30:
        case 0x31:
            drm_mode = mode;
            ESP_LOGI(TAG, "Host requested 0x%02x DRM mode (implemented)", mode);
            break;
        default:
            ESP_LOGW(TAG, "Host requested unimplemented DRM mode 0x%02x", mode);
            drm_mode = 0x31;
            break;
    }
}
/**
 * TODO: Elaborate
 * @param common_byte
 */
void gamepad_set_leds(uint8_t common_byte) {
    led_state = common_byte & LED_ALL;
    ESP_LOGI(TAG, "LEDs set: 0x%02X", led_state);
}

void gamepad_set_rumble(bool enabled) {
    rumble_state = enabled;
    if (enabled) {
        ESP_LOGI(TAG, "Rumble: ON");
    }
    else {
        ESP_LOGI(TAG, "Rumble: OFF");
    }
    //TODO: Add actual rumble motor code here
}

void gamepad_send_status_report() {
    uint8_t report[REPORT_SIZE_0x20] = {0};
    report[0] = 0x00; //BB1
    report[1] = 0x00; //BB2
    report[2] = led_state;
    report[3] = 0x00; //unused
    report[4] = 0x00; //unused
    report[5] = 0xFF; //battery level: report as full

    esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0x20, REPORT_SIZE_0x20, report);
    ESP_LOGI(TAG, "Sent status report (0x20): LF=0x%02X", report[2]);
}

void gamepad_send_acknowledgement(uint8_t report_id, uint8_t error) {
    uint8_t report[REPORT_SIZE_0x22] = {0};
    report[0] = 0x00; //BB1
    report[1] = 0x00; //BB2
    report[2] = report_id;
    report[3] = error;
    esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, 0x20, REPORT_SIZE_0x20, report);
    ESP_LOGI(TAG, "Sent acknowledgement (0x22): LF=0x%02X, error flag = %d", report_id, error);
}

/**
 * Converts a g-value into the Wiimote-style 10-bit unsigned range.
 * Clamped on both end so that a large negative float wraps to 0
 * instead of to large number.
 * @param value the number that is being converted.
 * @return the value converted to a 10-bit unsigned range.
 */
static uint16_t to_10bit(float value) {
    int32_t scaled = (int32_t) (value * 170.0f + 512.0f);
    if (scaled < 0) {
        scaled = 0;
    }
    if (scaled > 1023) {
        scaled = 1023;
    }
    return (uint16_t)scaled;
}
/**
 * Packs buttons (and, in 0x31 mode, the 10-bit accel axes) into the needed report style the Wiimote
 * uses and then sends it.
 * This is the only instance which a report gets constructed so gamepad_task and
 * send_gamepad_last_report can't disagree about the layout.
 *
 * bb1 and bb2 are already built with the correct button bit positions in gamepad.h
 *
 * The msb and lsb lines mirror the real Wiimote's precision loss rather than introducing
 * our own: we take bit 1 of the 2-bit remainder and drop bit 0, since the 10th bit for Y
 * and Z don't exist.
 *
 * @param bb1 byte 1 (buttons + X-accel LSB)
 * @param bb2 byte 2 (buttons + Y-accel + Z-accel LSB)
 * @param x10
 * @param y10
 * @param z10
 */
static void send_gamepad_report(uint8_t bb1, uint8_t bb2, uint16_t x10, uint16_t y10, uint16_t z10)
{
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);

    uint8_t report_id;
    uint8_t report_len;

    if (drm_mode == 0x30) {
        buffer[0] = bb1;
        buffer[1] = bb2;
        report_id = 0x30;
        report_len = REPORT_SIZE_0x30;
    }
    else {
        uint8_t x_msb = (uint8_t) (x10 >> 2);
        uint8_t y_msb = (uint8_t) (y10 >> 2);
        uint8_t z_msb = (uint8_t) (z10 >> 2);
        uint8_t x_lsb = (uint8_t) (x10 & 0x03); //BB1 bits 5-6
        uint8_t y_lsb = (uint8_t) ((y10 >> 1) & 0x01); //BB2 bit 5
        uint8_t z_lsb = (uint8_t) ((z10 >> 1) & 0x01); //BB2 bit 6

        bb1 |= (uint8_t) (x_lsb << 5);
        bb2 |= (uint8_t) (y_lsb << 5);
        bb2 |= (uint8_t) (z_lsb << 6);

        buffer[0] = bb1;
        buffer[1] = bb2;
        buffer[2] = x_msb;
        buffer[3] = y_msb;
        buffer[4] = z_msb;

        report_id = 0x31;
        report_len = REPORT_SIZE_0x31;
    }
    last_report_id  = report_id;
    last_report_len = report_len;

    esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, GAMEPAD_REPORT_ID,
                                  GAMEPAD_REPORT_SIZE, buffer);
    xSemaphoreGive(mutex);
}

/**
 * Resends whatever report was last built by send_gamepad_report. Used for
 * ESP_HIDD_GET_REPORT_EVT so that it recives a stale value rather than a hardcoded
 * fake one.
 */
 void send_gamepad_last_report() {
     if (!mutex) return;
     xSemaphoreTake(mutex, portMAX_DELAY);
     esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, GAMEPAD_REPORT_ID,
                                  GAMEPAD_REPORT_SIZE, buffer);
     xSemaphoreGive(mutex);
 }

// ---------- Tasks ---------- //
/**
 * Determines the state of the connection LED, meant to mimic that of a wiimote's LEDs.
 * if the ESP is connected to a host, the LED will be solid. Otherwise, it blinks on
 * and off.
 * @param pvParameters
 */
static void led_task(void *pvParameters)
{
    while (true) {
        if (connection) {
            gpio_set_level(LED_PIN, 0);
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

/**
 * Main polling loop: reads 11 button GPIOs, the accelerometer, converts accel
 * to 10-bit range (by calling to_10bit), and sends a report every 20ms (by calling
 * send_gamepad_report).
 * Runs from gamepad_task_start until the task is deleted by gamepad_task_stop.
 *
 * A failed accel read falls back to a neutral (512,512,512) reading for that cycle
 * so that button input is not affected.
 *
 * The 20ms send interval matches Window's Bluetooth sniff-mode interval (~11.25ms
 * QoS access_latency set in main.c)
 * @param pvParameters
 */
static void gamepad_task(void *pvParameters)
{
    ESP_LOGI(TAG, "gamepad task started");
    uint16_t last_buttons = 0xFFFF;
    int tick = 0;

    while (true) {
        // Buttons
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


        uint8_t bb1 =
                (button_left  ? BB1_LEFT  : 0) |
                (button_right ? BB1_RIGHT : 0) |
                (button_down  ? BB1_DOWN  : 0) |
                (button_up    ? BB1_UP    : 0) |
                (button_plus  ? BB1_PLUS  : 0);

        uint8_t bb2 =
                (button_2     ? BB2_TWO   : 0) |
                (button_1     ? BB2_ONE   : 0) |
                (button_b     ? BB2_B     : 0) |
                (button_a     ? BB2_A     : 0) |
                (button_minus ? BB2_MINUS : 0) |
                (button_home  ? BB2_HOME  : 0);

        //For testing button outputs from monitor's end
        uint16_t buttons_debug = ((uint16_t)bb2 << 8) | bb1;
        if (buttons_debug != last_buttons) {
            ESP_LOGI(TAG, "GPIO raw: UP=%d DN=%d LT=%d RT=%d A=%d B=%d -=%d HOME=%d +=%d 1=%d 2=%d",
                     button_up, button_down, button_left, button_right,
                     button_a, button_b, button_minus, button_home,
                     button_plus, button_1, button_2);
            ESP_LOGI(TAG, "BB1=0x%02X BB2=0x%02X", bb1, bb2);
            last_buttons = buttons_debug;
        }
        // Acceleration
        accel_g_t accel = {0};
        esp_err_t accel_err = read_g_values(&accel_device, &accel);

        //read neutral on failure
        uint16_t accel_x = 512;
        uint16_t accel_y = 512;
        uint16_t accel_z = 512;

        // converts the read g values to wiimote 10-bit range
        if (accel_err == ESP_OK) {
            accel_x = to_10bit(accel.x);
            accel_y = to_10bit(accel.y);
            accel_z = to_10bit(accel.z);

        }

        // ---------- Testing from monitor end ---------- //
        if (ACCEL_DEBUG_LOG) {
            if (++tick % 20 == 0) {
                ESP_LOGI(TAG, "g:[%+.3f %+.3f %+.3f]  hid10:[%4u %4u %4u]",
                          accel.x, accel.y, accel.z, accel_x, accel_y, accel_z);
            }
        }

        send_gamepad_report(bb1, bb2, accel_x, accel_y, accel_z);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
/**
 * Initializes I2c that the MPU6050 needs to function. Internal pull-ups are enabled
 * rather than relying on external ones on the breakout board.
 * ESP_ERROR_CHECK is used because a failure here means that the accelerometer wont
 * work for the rest of the program's lifetime.
 * @return bus_handle
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
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "MPU-6050 initialized");
    return bus_handle;
}

/**
 * A one-time setup, called from main.c before Bluetooth comes up.
 * initializes i2c, the MPU6050, confiures all 11 buttons to pull-up
 * inputs, sets up the connection-status LED, and starts led_task.
 *
 * Does NOT call gamepad_task so that reports aren't sent until after
 * a host is connected, so that there is a place for reports to get
 * sent to.
 */
void gamepad_init()
{
    // Acceleration
    i2c_master_bus_handle_t bus = i2c_init();
    ESP_ERROR_CHECK(accel_init(&accel_device, bus, ACCEL_RANGE_2G));

    // Buttons
    const int button_pins[] = {
            BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT,
            BUTTON_A, BUTTON_B,
            BUTTON_MINUS, BUTTON_HOME, BUTTON_PLUS,
            BUTTON_1, BUTTON_2
    };
    int button_size = sizeof(button_pins) / sizeof(button_pins[0]);

    for (int i = 0; i < button_size; i++) {
        gpio_set_direction(button_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(button_pins[i], GPIO_PULLUP_ONLY);
    }

    // LED
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    xTaskCreate(led_task, "led_task", 1024, NULL, configMAX_PRIORITIES - 3, NULL);

    ESP_LOGI(TAG, "gamepad initialized");
}

/**
 * Starts polling/sending reports. Called from ESP_HIDD_OPEN_EVT once a host acttually connects.
 * The mutex is created here so that there's no window where any mutex from a previous connection could
 * be reused.
 */
void gamepad_task_start()
{
    mutex = xSemaphoreCreateMutex();
    xTaskCreate(gamepad_task, "gamepad_task", 8 * 1024, NULL,
                configMAX_PRIORITIES - 3, &handle_task);
}

/**
 * Stops polling/sending reports on disconnect (called from ESP_HIDD_CLOSE_EVT or ESP_HIDD_VC_UNPLUG_EVT
 * in main.c). Deletes the task by vTaskDelete, unlike the Rust version's gamepad_task, which polls an
 * atomic flag each loop iteration.
 *
 * Guards both handle_task and mutex against being NULL so even that if the task was never started it doesn't
 * break if called. Nulls both out after so a repeat call does not result in a double-free.
 */
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