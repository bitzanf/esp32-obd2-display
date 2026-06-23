#include <cstdio>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "ST7701S.h"
#include "GT911.h"
#include "LVGL_Driver.h"

#include "BleManager.hpp"
#include "MockImpl.hpp"
#include "Obd2Manager.hpp"
#include "UIManager.hpp"

BleManager* ble;
Obd2Manager* obd2;
UIManager* ui;

void NVS_Init() {
    // Initialize NVS - stores PHY calibration for BLE
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
}

void Driver_Init()
{
    NVS_Init();
    I2C_Init();
    Touch_Init();
    EXIO_Init();
}

void Application_Init() {
    ble = new BleManager;
    obd2 = new Obd2Manager(ble);
    ui = new UIManager(obd2);
}

void Application_Start() {
    ble->startScanAndConnect();
    if (!ble->isConnected()) {
        ESP_LOGW("MAIN", "Waiting for BLE connection...");
    }

    while (!ble->isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    obd2->initAdapter();
    ui->startPollingTask();
}

void MockApp_Init() {
    static MockObdDataProvider mockObd;
    ui = new UIManager(&mockObd);

    ui->startPollingTask();
}

[[noreturn]] void u_main() {
    Driver_Init();
    LCD_Init();
    LVGL_Init();

    Application_Init();
    Application_Start();
    //MockApp_Init();

    while (true) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
        ui->processUIUpdates();
    }
}

extern "C" void app_main(void)
{
    try {
        u_main();
    } catch (std::exception &e) {
        ESP_LOGE("APP_MAIN", "Exception: %s", e.what());
    }
}
