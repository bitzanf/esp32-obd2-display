#include "UIManager.hpp"
#include "Obd2Manager.hpp"

#include <stdexcept>

#include "esp_log.h"

UIManager::UIManager(Obd2Manager &obd2Manager) : obd2(&obd2Manager) { // NOLINT(*-pro-type-member-init)
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
}

void UIManager::initPidTracking() {
    trackedPids = {
        {0x0C, rpmLabel, [](const std::span<const uint8_t> data) {
            if (data.size() < 2) return std::string("---");
            const int rpm = ((data[0] << 8) | data[1]) / 4;
            return "RPM: " + std::to_string(rpm);
        }},
        {0x0D, speedLabel, [](const std::span<const uint8_t> data) {
            if (data.empty()) return std::string("---");
            const int speed = data[0];
            return "Speed: " + std::to_string(speed) + " km/h";
        }},
        {0x05, coolantLabel, [](const std::span<const uint8_t> data) {
            if (data.empty()) return std::string("---");
            const int temp = data[0] - 40;
            return "Coolant: " + std::to_string(temp) + " °C";
        }}
    };
}

void UIManager::pollRegisteredPids() {
    std::string command;
    for (const auto& [pid, target, parser] : trackedPids) {
        try {
            command = std::format("01 {:02X}", pid);

            const auto response = obd2->sendCommand(command);
            const auto bytes = Obd2Manager::hexStringToBytes(response);

            if (bytes.size() < 2) {
                ESP_LOGW(UI_TAG, "Received too short response for PID %s (%s)", command.c_str(), response.c_str());
                continue;
            }

            if (bytes[0] != 0x41) {
                ESP_LOGW(UI_TAG, "Unexpected response for PID %s: %s", command.c_str(), response.c_str());
                continue;
            }

            if (bytes[1] != pid) {
                ESP_LOGW(UI_TAG, "Response PID mismatch for %s: %s", command.c_str(), response.c_str());
                continue;
            }

            std::span dataSpan(bytes.begin() + 2, bytes.end());
            std::string parsed = parser(dataSpan);
            std::string parsedText = parsed.empty() ? "---" : parsed;

            UIUpdateMessage msg{};
            msg.target = target;
            strncpy(msg.text, parsedText.c_str(), sizeof(msg.text) - 1);

            xQueueSend(uiQueueHandle, &msg, portMAX_DELAY);
        } catch (const std::exception& e) {
            ESP_LOGE(UI_TAG, "Error polling PID %s: %s", command.c_str(), e.what());
        }
        // slight delay between individual PID queries
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
