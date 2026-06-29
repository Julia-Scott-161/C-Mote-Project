/// Author: Julia Scott (Julia-Scott-161)

#ifndef EMULATED_WIIMOTE_GAMEPAD_H
#define EMULATED_WIIMOTE_GAMEPAD_H
#include "accel.h"
//#include <stdint.h>
#include <stdbool.h>
//#include <math.h>

//#include "esp_log.h"
//#include "esp_err.h"
//#include "esp_check.h"
#include "esp_hidd_api.h"
#include "driver/gpio.h"
//#include "driver/i2c_master.h"

//#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"



//------------------- Definitions: Buttons -------------------//
#define BUTTON_A                21
#define BUTTON_B                19
#define BUTTON_MINUS            14
#define BUTTON_HOME             23
#define BUTTON_PLUS             13
#define BUTTON_1                18
#define BUTTON_2                25
#define BUTTON_UP               22
#define BUTTON_DOWN             26
#define BUTTON_LEFT             27
#define BUTTON_RIGHT            5
//LED pin for identifying connection status
#define LED_PIN                 20

//------------------- Definitions: Acceleration -------------------//
//#define I2C_PORT    I2C_NUM_0
#define I2C_SDA     4
#define I2C_SCL     22
//#define ACCEL_ADDRESS_LOW 0x68  // AD0 low (connected to GND)
//#define ACCEL_POWER_CTRL_1  0x6B
#define ACCEL_OUTPUT  0x3B

//for testing purposes only: remove and/or swap to 0 when not needed
#define ACCEL_DEBUG_LOG 1

//------------------- Definitions: Report -------------------//
#define GAMEPAD_REPORT_ID       (0x00)
#define GAMEPAD_REPORT_SIZE     (8)

void gamepad_set_connected(bool connected);
void gamepad_init();
void gamepad_task_start();
void gamepad_task_stop();
void send_gamepad_report(uint16_t buttons);
#endif //EMULATED_WIIMOTE_GAMEPAD_H
