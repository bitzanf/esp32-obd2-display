#include "UIManager.hpp"
#include "Obd2Manager.hpp"

#include <stdexcept>

#include "esp_log.h"

UIManager::UIManager(IObd2* obd2Manager) : obd2(obd2Manager) { // NOLINT(*-pro-type-member-init)
    assert(obd2);

    uiQueueHandle = xQueueCreate(10, sizeof(UIUpdateMessage));
    if (uiQueueHandle == nullptr) {
        throw std::runtime_error("Failed to create UI update queue");
    }
    pollingTaskHandle = nullptr;

    createLayout();
    initPidTracking();
}

UIManager::~UIManager() {
    if (pollingTaskHandle) vTaskDelete(pollingTaskHandle);
    if (uiQueueHandle) vQueueDelete(uiQueueHandle);

    if (rpmLabel) lv_obj_del(rpmLabel);
}

void UIManager::processUIUpdates() const {
    UIUpdateMessage msg; // NOLINT(*-pro-type-member-init)

    while (xQueueReceive(uiQueueHandle, &msg, 0) == pdTRUE) {
        if (msg.target != nullptr) {
            lv_label_set_text(msg.target, msg.text);
        }
    }
}

void UIManager::startPollingTask() {
    if (pollingTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            pollingTask,
            "UI_Polling_Task",
            4096,
            this,
            5,
            &pollingTaskHandle,
            1
        );
    }
}

void UIManager::createLayout() {
    auto* scr = lv_scr_act();

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_16);

    // RPM label
    rpmLabel = lv_label_create(scr);
    lv_obj_add_style(rpmLabel, &style, 0);
    lv_label_set_text(rpmLabel, "RPM: ---");
    lv_obj_align(rpmLabel, LV_ALIGN_TOP_MID, 0, 40);
/*
    // speed label
    speedLabel = lv_label_create(scr);
    lv_obj_add_style(speedLabel, &style, 0);
    lv_label_set_text(speedLabel, "Speed: ---");
    lv_obj_align(speedLabel, LV_ALIGN_CENTER, 0, -10);

    // coolant temperature
    coolantLabel = lv_label_create(scr);
    lv_obj_add_style(coolantLabel, &style, 0);
    lv_label_set_text(coolantLabel, "Coolant: ---");
    lv_obj_align(coolantLabel, LV_ALIGN_BOTTOM_MID, 0, -40);
*/
}

void UIManager::initPidTracking() {
    // the car is a 1999/2000 shitbox, so it of course doesn't speak standard OBD-II
    // the values are proprietary ('0x21' = KWP-2000 read at address) and undocumented unfortunately
    // so RPM is about the only thing we can reliably get...
    // AlfaOBD is the only app that seems to understand every value from the ECU
    trackedPids = {
        {"21301", rpmLabel, [](const std::span<const uint8_t> data) -> std::string {
            if (data.size() < 2) return "---";
            const int rpm = (data[0] << 8) | data[1];
            return "RPM: " + std::to_string(rpm);
        }}
    };
}

void UIManager::pollRegisteredPids() {
    for (const auto& [command, target, parser] : trackedPids) {
        try {
            const auto response = obd2->sendCommand(command, Obd2Manager::DEFAULT_TIMEOUT_MS);
            const auto bytes = Obd2Manager::hexStringToBytes(response);

            if (bytes.size() < 2) continue;

            // KWP2000 Mode 21 Success Response always starts with 0x61
            if (bytes[0] != 0x61) {
                ESP_LOGW(UI_TAG, "Unexpected response for %s: %s", command.c_str(), response.c_str());
                continue;
            }

            // extract the payload (skip '61' and PID echo)
            std::span dataSpan(bytes.begin() + 2, bytes.end());
            std::string parsed = parser(dataSpan);
            std::string parsedText = parsed.empty() ? "---" : parsed;

            UIUpdateMessage msg{};
            msg.target = target;
            strncpy(msg.text, parsedText.c_str(), sizeof(msg.text) - 1);

            xQueueSend(uiQueueHandle, &msg, portMAX_DELAY);
        } catch (const std::exception& e) {
            ESP_LOGE(UI_TAG, "Error polling %s: %s", command.c_str(), e.what());
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool UIManager::waitForAdapter() const {
    if (!obd2->isConnected()) {
        ESP_LOGW(UI_TAG, "OBD-II adapter not connected. Waiting...");

        while (!obd2->isConnected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        ESP_LOGI(UI_TAG, "OBD-II adapter connected");
        try {
            obd2->initAdapter();
        } catch (const std::exception& e) {
            ESP_LOGE(UI_TAG, "Failed to initialize OBD-II adapter: %s", e.what());
            return false;
        }
    }
    return true;
}

void UIManager::pollingTask(void *param) {
    auto* self = static_cast<UIManager*>(param);
    assert(self != nullptr);

    ESP_LOGI(UI_TAG, "UI Polling Task started");
    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        // ReSharper disable once CppDFANullDereference
        if (!self->waitForAdapter()) continue;

        self->pollRegisteredPids();

        // slight delay between value updates
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
