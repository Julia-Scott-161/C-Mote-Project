/// Author: Julia Scott (Julia-Scott-161)

#ifndef EMULATED_WIIMOTE_GAMEPAD_H
#define EMULATED_WIIMOTE_GAMEPAD_H
#include "accel.h"
#include <stdbool.h>
#include "esp_hidd_api.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

//------------------- Definitions: Buttons -------------------//
// Following typical Wiimote protocol bit layout
// Byte 1: (bits 0-4)
#define BUTTON_LEFT             25 //currently not working
#define BUTTON_RIGHT            33 //currently not working
#define BUTTON_DOWN             26 //currently not working
#define BUTTON_UP               27 //currently not working
#define BUTTON_PLUS             21
//Byte 2: (bits 0-4 and 7)
#define BUTTON_2                15
#define BUTTON_1                5
#define BUTTON_B                14
#define BUTTON_A                32 //Currently causing a power_on reset once pressed.
#define BUTTON_MINUS            18
#define BUTTON_HOME             19

//LED pin for identifying connection status
#define LED_PIN                 23

//------------------- Definitions: Acceleration -------------------//
#define I2C_SDA     4
#define I2C_SCL     22

//for testing purposes only: remove and/or swap to 0 when not needed
#define ACCEL_DEBUG_LOG 1

//------------------- Definitions: Report -------------------//
#define GAMEPAD_REPORT_ID       (0x00)
#define GAMEPAD_REPORT_SIZE     (5)

void gamepad_set_connected(bool connected);
void gamepad_init();
void gamepad_task_start();
void gamepad_task_stop();
void send_gamepad_last_report();
#endif //EMULATED_WIIMOTE_GAMEPAD_H
