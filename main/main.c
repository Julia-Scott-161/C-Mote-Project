/** This project uses ESP-IDF and an ESP32 to recreate a Wii remote using bluetooth
  * classic. Like the Wii remote, this program is meant to support 11 buttons, and
  * uses a MPU6050 for acceleration values.
  *
  * This begins to branch away from a typical bluetooth HID controller to a more
  * faithful implementation of the Wiimote protocol.

  * As it's meant to recreate the original Wii remote, the Wii Motion Control Plus
  * (gyroscope values) will not be supported, despite the MPU6050 having the ability
  * to measure them.
  *
  * Author: Julia Scott (Julia-Scott-161)
  **/

#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_gap_bt_api.h"
#include "esp_sdp_api.h"
#include <string.h>
#include <inttypes.h>

#include "gamepad.h"

static const char *TAG = "Main/Bluetooth";
static const char device_name[] = "Nintendo RVL-CNT-01"; //official name

// ---------- Device Identification (SDP DIP record) ---------- //
//Real Wii remotes report these over the Device ID profile SDP record.

#define WIIMOTE_VENDOR_ID 0x057e //Nintendo Co. Ltd.
#define WIIMOTE_PRODUCT_ID 0x0306 //RVL-CNT-01
//schrodinger's code: works until otherwise disproved
#define WIIMOTE_VERSION 0x0100 //PLACEHOLDER

// ---------- HID Descriptor ---------- //
static uint8_t HIDD[] = {
        0x05, 0x01, //Usage Page (Generic Desktop)
        0x09, 0x05, //Usage Page (Gamepad)
        0xa1, 0x01, //Collection (Application)

        //Byte 0: buttons 1-5, X-accel LSBs + pad
        0x05, 0x09, //Usage Page (Button)
        0x19, 0x01, //Usage Minimum (Button 1)
        0x29, 0x05, //Usage Maximum (Button 5)
        0x15, 0x00, //Logical Minimum (0)
        0x25, 0x01, //Logical Maximum (1)
        0x95, 0x05, //Report Count (5)
        0x75, 0x01, //Report Size (1 bit)
        0x81, 0x02, //Input (Data, Variable, Absolute)  <- buttons 1-5
        0x95, 0x03, //Report Count (3)
        0x75, 0x01, //Report Size (1)
        0x81, 0x03, //Input (Constant)

        //Byte 1: buttons 6-10, Y/Z LSBs, button 11 (Home)
        0x19, 0x06, //Usage Minimum (Button 6)
        0x29, 0x0A, //Usage Maximum (Button 10)
        0x95, 0x05, //Report Count (5)
        0x75, 0x01, //Report Size (1)
        0x81, 0x02, //Input (Data, Variable, Absolute)  <- buttons 1-5
        0x95, 0x02, //Report Count (2)
        0x75, 0x01, //Report Size (1)
        0x81, 0x03, //Input (Constant) <- Y-Accel LSB, Z-Accel LSB
        0x19, 0x0B, //Usage Minimum (Button 11)
        0x29, 0x0B, //Usage Maximum (Button 11)
        0x95, 0x01, //Report Count (1)
        0x75, 0x01, //Report Size (1)
        0x81, 0x02, //Input (Data, Variable, Absolute)  <- buttons 1-5

        //Bytes 2-4: Accel MSBs
        0x05, 0x01, //Usage Page (Generic Desktop)
        0x09, 0x30, //Usage (X)
        0x09, 0x31, //Usage (Y)
        0x09, 0x32, //Usage (Z)
        0x15, 0x00, //Logical Minimum (0)
        0x26, 0xFF, 0x00, //Logical Maximum (255)
        0x75, 0x08, //Report Size (8)
        0x95, 0x03, //Report Count (3)
        0x81, 0x02, //Input (Data, Variable, Absolute) <- X, Y, Z accel bytes

        0xc0 //End Collection
};

// ---------- HID App Parameters ---------- //
/** the configuration needed to register a Bluetooth HID device.
  * app_param holds the name, description, provider, subclass,
  * and a pointer to the HIDD defined above and its length.
  * both_qos defines quality of Service parameters for the HID
  * connection, applying to both L2CAP channels.
  * protocol_mode is a single byte which defines the mode the
  * device starts in.
 **/
typedef struct {
    esp_hidd_app_param_t app_param;
    esp_hidd_qos_param_t both_qos;
    uint8_t protocol_mode;
} hid_params_t;

static hid_params_t controller_hid = {0};

static char *bluetooth_address_to_string(esp_bd_addr_t bt_address, char *string) {
    if (bt_address == NULL || string == NULL) {
        return NULL;
    }
    uint8_t *temp_p = bt_address;
    //sprintf writes formatted text into a string buffer
    sprintf(string, "%02x:%02x:%02x:%02x:%02x:%02x", temp_p[0], temp_p[1], temp_p[2], temp_p[3], temp_p[4], temp_p[5]);
    return string;
}

// ---------- GAP Event Callback ---------- //
void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
            } else {
                ESP_LOGE(TAG, "Authentication failed: %d", param->auth_cmpl.stat);
            }
            break;
            // For the 1+2 button method, The Wii remote uses the pin of the
            // bluetooth address of the wii remote written backwards.
            // For the sync button, it is the bluetooth address of your PC
            // written backwards.
            // This code does not currently require either, it uses 1234.
            // note here - currently not correct, but also not causing problems.
        case ESP_BT_GAP_PIN_REQ_EVT:
            if (param->pin_req.min_16_digit) {
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            } else {
                esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(TAG, "Bluetooth mode changed: %d", param->mode_chg.mode);
            break;
        default:
            break;
    }
}

// ---------- HID Callback ---------- //
void esp_bt_hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param) {

    switch (event) {
        case ESP_HIDD_INIT_EVT:
            if (param->init.status == ESP_HIDD_SUCCESS) {
                esp_bt_hid_device_register_app(&controller_hid.app_param, &controller_hid.both_qos,
                                               &controller_hid.both_qos);
            } else {
                ESP_LOGE(TAG, "HIDD initialization failed");
            }
            break;
        case ESP_HIDD_REGISTER_APP_EVT:
            if (param->register_app.status == ESP_HIDD_SUCCESS) {
                ESP_LOGI(TAG, "Now discoverable");
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                // Same as previous note. Not correct, but not causing issues
                if (param->register_app.in_use) {
                    ESP_LOGI(TAG, "reconnecting to known host");
                    esp_bt_hid_device_connect(param->register_app.bd_addr);
                }
            } else {
                ESP_LOGE(TAG, "hidd register failed");
            }
            break;
        case ESP_HIDD_OPEN_EVT:
            if (param->open.status == ESP_HIDD_SUCCESS) {
                if (param->open.conn_status == ESP_HIDD_CONN_STATE_CONNECTING) {
                    ESP_LOGI(TAG, "connecting...");
                } else if (param->open.conn_status == ESP_HIDD_CONN_STATE_CONNECTED) {
                    ESP_LOGI(TAG, "connected");
                    gamepad_set_connected(true);
                    gamepad_task_start();
                    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
                }
            }
            else {
                ESP_LOGE(TAG, "Open failed");
            }
            break;
        case ESP_HIDD_CLOSE_EVT:
            if (param->close.status == ESP_HIDD_SUCCESS) {
                if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTING) {
                    ESP_LOGI(TAG, "disconnecting...");
                }
                else if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTED) {
                    ESP_LOGI(TAG, "disconnected");
                    gamepad_set_connected(false);
                    gamepad_task_stop();
                    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                }
            }
            else {
                ESP_LOGE(TAG, "close failed");
            }
            break;

        case ESP_HIDD_VC_UNPLUG_EVT:
            if (param->vc_unplug.status == ESP_HIDD_SUCCESS) {
                if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTED) {
                    ESP_LOGI(TAG, "virtual cable unplugged");
                    gamepad_set_connected(false);
                    gamepad_task_stop();
                    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                }
            }
            break;
        case ESP_HIDD_GET_REPORT_EVT:
            send_gamepad_last_report();
            break;
        case ESP_HIDD_SET_REPORT_EVT: {
            // Logs each output report from the host so the wiimote protocol
            // is exposed and can be implemented easier, instead of just guessing
            // where it fails or what the bytes mean.
            const uint8_t *desc = param -> set_report.data;
            uint16_t len = param -> set_report.len;
            char hex[64] = {0};
            int position = 0;
            for (int i = 0; i < len && position < (int) sizeof(hex) - 3; i++) {
                position += snprintf(hex + position, sizeof(hex) - position, "%02x ", desc[i]);
            }
            ESP_LOGW(TAG, "SET_REPORT: type=%d id=0x%02x len=%d data=[ %s]",
                     param->set_report.report_type, param->set_report.report_id, len, hex);

            if (param->set_report.report_id == 0x12 && len >= 2) {
                gamepad_set_drm_mode(desc[1]);
            }
            break;
        }
        case ESP_HIDD_INTR_DATA_EVT: {
            // Wiimotes send output reports over the INTERRUPT channel, rather than
            // the typical SET_REPORT on the control channel.
            const uint8_t *desc = param -> intr_data.data;
            uint16_t len = param -> intr_data.len;
            char hex[64] = {0};
            int position = 0;
            for (int i = 0; i < len && position < (int)sizeof(hex) - 3; i++) {
                position += snprintf(hex + position, sizeof(hex) - position, "%02x ", desc[i]);
            }
            ESP_LOGW(TAG, "INTR_DATA: id=0x%02x len=%d data=[ %s]",
                     param->intr_data.report_id, len, hex);

            if (param->intr_data.report_id == 0x12 && len >= 2) {
                gamepad_set_drm_mode(desc[1]);
            }
            break;
        }
        case ESP_HIDD_SEND_REPORT_EVT:
            if (param->send_report.status != ESP_HIDD_SUCCESS) {
                ESP_LOGE(TAG, "send report failed: id:0x%02x status:%d reason:%d",
                         param->send_report.report_id,
                         param->send_report.status,
                         param->send_report.reason);
            }
            break;
        case ESP_HIDD_SET_PROTOCOL_EVT:
            controller_hid.protocol_mode = param->set_protocol.protocol_mode;
            ESP_LOGI(TAG, "protocol mode: %s",
                     controller_hid.protocol_mode == ESP_HIDD_BOOT_MODE ? "boot" : "report");
            break;
        default:
            break;
    }
}

// ---------- SDP DIP Callback (Vendor/Product ID) ---------- //
/**
 * Handles the SDP events. ESP_SDP_INIT_EVT builds and registers a Device ID Profile (DIP)
 * record that advertises the Wiimotes's VendorID/ProductID.
 *
 * ESP-IDF has a known issue where the VID/PID can show as 0xFFFF/0x0000 on the host side in
 * Classic BT mode with a different HIDD API. It is unclear whether the SDP-record approach
 * is affected the same way, so ESP_LOGW confirms what the stack was asked to advertise, so
 * that for any issues on the host side, it can be ruled out that it's an issue with what was
 * asked
 */
 static void esp_sdp_cb(esp_sdp_cb_event_t event, esp_sdp_cb_param_t *param) {
     switch (event) {
         case ESP_SDP_INIT_EVT:
             if (param->init.status == ESP_SDP_SUCCESS) {
                 esp_bluetooth_sdp_dip_record_t dip_record = {
                         .hdr = {
                                 .type = ESP_SDP_TYPE_DIP_SERVER,
                         },
                         .vendor           = WIIMOTE_VENDOR_ID,
                         .vendor_id_source = ESP_SDP_VENDOR_ID_SRC_BT,
                         .product          = WIIMOTE_PRODUCT_ID,
                         .version          = WIIMOTE_VERSION,
                         .primary_record   = true,
                 };
                 ESP_LOGW(TAG, "Requesting DIP record: VID=0x%04x PID=0x%04x version=0x%04x ",
                          dip_record.vendor, dip_record.product, dip_record.version);
                          esp_sdp_create_record((esp_bluetooth_sdp_record_t *)&dip_record);
             }
             else {
                 ESP_LOGE(TAG, "SDP init failed: status=%d", param -> init.status);
             }
             break;
         case ESP_SDP_CREATE_RECORD_COMP_EVT:
             ESP_LOGI(TAG, "DIP record created: status=%d handle=0x%x",
                      param->create_record.status, param -> create_record.record_handle);
             break;
         case ESP_SDP_DEINIT_EVT:
             ESP_LOGI(TAG, "SDP deinitialized: status=%d", param -> deinit.status);
             break;
         default:
             break;
     }
}

// ---------- Main ---------- //

void app_main(void) {
    char bluetooth_address_string[18] = {0};

    esp_err_t return_code = nvs_flash_init();
    if (return_code == ESP_ERR_NVS_NO_FREE_PAGES || return_code == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        return_code = nvs_flash_init();
    }

    ESP_ERROR_CHECK(return_code);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bluedroid_config));
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(esp_bt_gap_cb));

    esp_bt_gap_set_device_name(device_name);
    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_GAMEPAD;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    //populates the SDP HID service record's attributes, separate from the GAP device name
    //TODO: no confirmed evidence that the wii protocol checks this.
    controller_hid.app_param.name           = "Nintendo RVL-CNT-01";
    controller_hid.app_param.description    = "Button + Accel Input";
    controller_hid.app_param.provider       = "ESP32";
    //TODO: may map to a CoD-adjacent value used within the SDP HID descriptor, separate
    // from the cod.minor set manually via esp_bt_gap_set_cod()
    controller_hid.app_param.subclass       = ESP_HID_CLASS_GPD;
    controller_hid.app_param.desc_list      = HIDD;
    controller_hid.app_param.desc_list_len  = sizeof(HIDD);
    memset(&controller_hid.both_qos, 0, sizeof(esp_hidd_qos_param_t));
    //setting these reduces potential latency issues when bluetooth is in sniff mode
    controller_hid.both_qos.service_type      = 1;          // best effort (0 = no traffic)
    controller_hid.both_qos.access_latency    = 11250;      // 11.25ms in microseconds

    controller_hid.protocol_mode = ESP_HIDD_REPORT_MODE;

    esp_bt_hid_device_register_callback(esp_bt_hidd_cb);
    esp_bt_hid_device_init();

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    ESP_LOGI(TAG, "own address: %s",
             bluetooth_address_to_string((uint8_t *)esp_bt_dev_get_address(), bluetooth_address_string));

    ESP_ERROR_CHECK(esp_sdp_register_callback(esp_sdp_cb));
    ESP_ERROR_CHECK(esp_sdp_init());

    gamepad_init();
}