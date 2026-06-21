#ifndef ESP32_OBD2_DISPLAY_UI_MANAGER_HPP
#define ESP32_OBD2_DISPLAY_UI_MANAGER_HPP

#include <functional>
#include <string>
#include <span>

#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define UI_TAG "UI_MGR"

class Obd2Manager;

struct UIUpdateMessage {
    lv_obj_t* target;
    char text[32];
};

struct ObdPid {
    uint8_t pid;
    lv_obj_t* target;
    std::function<std::string(std::span<const uint8_t>)> parser;
};

class UIManager {
public:
    explicit UIManager(Obd2Manager& obd2Manager);
    ~UIManager();

    UIManager(const UIManager& other) = delete;
    UIManager& operator=(const UIManager& other) = delete;

    // to be called from the UI thread
    void processUIUpdates() const;

    void startPollingTask();

private:
    Obd2Manager* obd2;
    TaskHandle_t pollingTaskHandle;
    QueueHandle_t uiQueueHandle;

    lv_obj_t* rpmLabel;
    lv_obj_t* speedLabel;
    lv_obj_t* coolantLabel;

    std::vector<ObdPid> trackedPids;

    void createLayout();
    void initPidTracking();

    void pollRegisteredPids();
    bool waitForAdapter() const;

    static void pollingTask(void* param);
};



#endif //ESP32_OBD2_DISPLAY_UI_MANAGER_HPP
