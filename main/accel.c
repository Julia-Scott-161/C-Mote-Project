/**
 * This is to recreate the acceleration function in a Wiimote.
 * While the original wiimote uses a ADXL330, this code is
 * written to be used with an MPU_6050 Accelerometer Driver.
 *
 *
 * Author: Julia Scott (Julia-Scott-161)
 */

#include "accel.h"
static const char *TAG = "Acceleration";

static float accel_sensitivity(accel_range_t range)
{
    switch (range) {
        case ACCEL_RANGE_2G:
            return 16384.0f;
        case ACCEL_RANGE_4G:
            return  8192.0f;
        case ACCEL_RANGE_8G:
            return  4096.0f;
        case ACCEL_RANGE_16G:
            return  2048.0f;
        default:
            return 16384.0f;
    }
}
// ---------- Internal Register Helpers ---------- //
static esp_err_t write_reg(accel_t *device, uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    return i2c_master_transmit(device->dev_handle,
                               buffer, sizeof(buffer),
                               pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t read_reg(accel_t *device, uint8_t reg, uint8_t *output)
{
    return i2c_master_transmit_receive(device->dev_handle,
                                       &reg, 1, output, 1,
                                       pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t read_bytes(accel_t *device, uint8_t reg, uint8_t *buffer, size_t len)
{
    return i2c_master_transmit_receive(device->dev_handle,
                                       &reg, 1, buffer, len,
                                       pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

// ---------- Sensor Initialization ----------- //
esp_err_t accel_init(accel_t *device,
                       i2c_master_bus_handle_t bus,
                       accel_range_t range)
{
    i2c_device_config_t device_config = {
            .device_address = ACCEL_ADDRESS_LOW,
            .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(
            i2c_master_bus_add_device(bus, &device_config, &device->dev_handle),
            TAG, "Failed to add I2C device"
    );
    device->range = range;

    //verify identity
    uint8_t who = 0;
    esp_err_t error = read_reg(device, ACCEL_WHO_AM_I, &who);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: %s", esp_err_to_name(error));
        return error;
    }
    if (who != 0x68) {
        ESP_LOGE(TAG, "unexpected WHO_AM_I: expected 0x68, received 0x%02X", who);
        return ESP_ERR_NOT_FOUND;
    }

    //Device Wake-Up
    error = write_reg(device, ACCEL_POWER_CTRL_1, 0x00);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "wake-up failed: %s", esp_err_to_name(error));
        return error;
    }

    //Sample Rate Division
    error = write_reg(device, ACCEL_SAMPLE_RATE, 0x09);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Sample rate division failed: %s", esp_err_to_name(error));
        return error;
    }

    //DLPF
    error = write_reg(device, ACCEL_DLPF, 0x04);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "DLPF config failed: %s", esp_err_to_name(error));
        return error;
    }

    //Range
    error = write_reg(device, ACCEL_RANGE, (uint8_t)(range << 3));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Acceleration config failed: %s", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "MPU_6050 initialised at address 0x%02X", ACCEL_ADDRESS_LOW);
    return ESP_OK;
}
esp_err_t read_raw_values(accel_t *device, accel_raw_t *output) {
    uint8_t buffer[6] = {0};
    esp_err_t error = read_bytes(device, ACCEL_OUTPUT_START, buffer, sizeof(buffer));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "accel raw read failed: %s", esp_err_to_name(error));
        return error;
    }

    output -> x = (int16_t)((buffer[0] << 8) | buffer[1]);
    output -> y = (int16_t)((buffer[2] << 8) | buffer[3]);
    output -> z = (int16_t)((buffer[4] << 8) | buffer[5]);
    return ESP_OK;
}

esp_err_t read_g_values(accel_t *device, accel_g_t *output) {
    accel_raw_t raw_values = {0};
    esp_err_t error = read_raw_values(device, &raw_values);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "accel read failed: %s", esp_err_to_name(error));
        return error;
    }

    float sensitivity = accel_sensitivity(device->range);

    output -> x = raw_values.x / sensitivity;
    output -> y = raw_values.y / sensitivity;
    output -> z = raw_values.z / sensitivity;
    return ESP_OK;
}

