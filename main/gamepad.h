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
#define BUTTON_LEFT             25
#define BUTTON_RIGHT            33
#define BUTTON_DOWN             26
#define BUTTON_UP               27
#define BUTTON_PLUS             21
//Byte 2: (bits 0-4 and 7)
#define BUTTON_2                15
#define BUTTON_1                5
#define BUTTON_B                14
#define BUTTON_A                13
#define BUTTON_MINUS            18
#define BUTTON_HOME             19

//LED pin for identifying connection status
#define LED_PIN                 23

//------------------- Definitions: Acceleration -------------------//
#define I2C_SDA     4
#define I2C_SCL     22

//for testing purposes only: remove and/or swap to 0 when not needed
#define ACCEL_DEBUG_LOG 0

//------------------- Definitions: Report -------------------//
#define REPORT_SIZE_0x30     2
#define REPORT_SIZE_0x31     5
#define REPORT_SIZE_0x20     6
#define REPORT_SIZE_0x22     4

//sized to the largest report currently implemented
#define GAMEPAD_REPORT_ID       0x20
#define GAMEPAD_REPORT_SIZE     REPORT_SIZE_0x20

//------------------- Definitions: Button Bit Positions -------------------//
#define BB1_LEFT    0x01
#define BB1_RIGHT   0x02
#define BB1_DOWN    0x04
#define BB1_UP      0x08
#define BB1_PLUS    0x10

#define BB2_TWO     0x01
#define BB2_ONE     0x02
#define BB2_B       0x04
#define BB2_A       0x08
#define BB2_MINUS   0x10
#define BB2_HOME    0x80

//------------------- Definitions: Status/LEDs -------------------//
//bit layout per WiiBrew and xwiimote protocol docs
#define LED_ALL 0x10



void gamepad_set_connected(bool connected);
void gamepad_init();
void gamepad_task_start();
void gamepad_task_stop();
void send_gamepad_last_report();
void gamepad_set_drm_mode(uint8_t mode);
void gamepad_set_leds(uint8_t common_byte);
void gamepad_set_rumble(bool enabled);
void gamepad_send_status_report();
void gamepad_send_acknowledgement(uint8_t report_id, uint8_t error);
#endif //EMULATED_WIIMOTE_GAMEPAD_H
