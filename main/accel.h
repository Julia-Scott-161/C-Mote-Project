/// Author: Julia Scott (Julia-Scott-161)
#ifndef EMULATED_WIIMOTE_ACCEL_H
#define EMULATED_WIIMOTE_ACCEL_H
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"




// ---------- I2C ---------- //
#define ACCEL_ADDRESS_LOW 0x68  // AD0 low
#define I2C_TIMEOUT_MS 100
#define I2C_FREQ    100000

// ---------- Register Map ---------- //

#define ACCEL_POWER_CTRL_1  0x6B
#define ACCEL_SAMPLE_RATE   0x19
#define ACCEL_DLPF          0x1A
#define ACCEL_RANGE         0x1C
#define ACCEL_OUTPUT_START  0x3B
#define ACCEL_WHO_AM_I      0x75

// ---------- Range and Data Types ---------- //

typedef enum {
    ACCEL_RANGE_2G = 0b00,
    ACCEL_RANGE_4G = 0b01,
    ACCEL_RANGE_8G = 0b10,
    ACCEL_RANGE_16G = 0b11,
} accel_range_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} accel_raw_t;

typedef struct {
    float x;
    float y;
    float z;
} accel_g_t;

typedef struct {
    i2c_master_dev_handle_t  dev_handle;
    accel_range_t range;
} accel_t;

// ---------- Functions ---------- //
/**
 * accel_init initialises the driver and wakes the MPU_6065 sensor.
 *
 * @param device Pointer to an uninitialized MPU_6050.
 * @param bus
 * @param range initial full-scale range.
 * @return ESP_OK if successful or an esp_err_t code.
 */
esp_err_t accel_init(accel_t *device,
                     i2c_master_bus_handle_t bus,
                     accel_range_t range);

esp_err_t read_raw_values(accel_t *device, accel_raw_t *output);
esp_err_t read_g_values(accel_t *device, accel_g_t *output);

#endif //EMULATED_WIIMOTE_ACCEL_H
