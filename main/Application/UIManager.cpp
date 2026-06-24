#include "UIManager.hpp"
#include "Obd2Manager.hpp"
#include "UIParsers/RPM.hpp"

#include <stdexcept>

#include "esp_log.h"
#include "UIParsers/SimpleLabel.hpp"

UIManager::UIManager(IObd2* obd2Manager) : obd2(obd2Manager) { // NOLINT(*-pro-type-member-init)
    assert(obd2);

    uiQueueHandle = xQueueCreate(10, sizeof(IObdPid*));
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

    if (rpmMeter) lv_obj_del(rpmMeter);
    if (speedLabel) lv_obj_del(speedLabel);
    if (temperatureLabel) lv_obj_del(temperatureLabel);
}

void UIManager::processUIUpdates() const {
    IObdPid* ptr; // NOLINT(*-pro-type-member-init)

    while (xQueueReceive(uiQueueHandle, &ptr, 0) == pdTRUE) {
        if (ptr) {
            ptr->updateUI();
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
    auto* display = lv_obj_get_disp(scr);
    const auto xres = lv_disp_get_hor_res(display);
    const auto yres = lv_disp_get_ver_res(display);

    auto* theme = lv_theme_default_init(
        display,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_CYAN),
        true,
        LV_FONT_DEFAULT
    );
    lv_disp_set_theme(display, theme);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_24);

    // RPM
    rpmMeter = lv_meter_create(scr);
    lv_obj_add_style(rpmMeter, &style, 0);
    lv_obj_center(rpmMeter);
    lv_obj_set_size(rpmMeter, xres, yres);

    constexpr int min = 0;
    constexpr int redline = 5000;
    constexpr int max = 7000;
    constexpr int major = 1000;
    constexpr int minor = 100;

    /*Add a scale first*/
    auto* scale = lv_meter_add_scale(rpmMeter);
    lv_meter_set_scale_ticks(rpmMeter, scale, (max-min)/minor + 1, 2, 10, lv_palette_main(LV_PALETTE_GREY));
    lv_meter_set_scale_major_ticks(rpmMeter, scale, major/minor, 4, 15, lv_color_black(), 40);
    lv_meter_set_scale_range(rpmMeter, scale, min, max, 270, 135);

    /*Add a red arc to the end*/
    auto* indic = lv_meter_add_arc(rpmMeter, scale, 3, lv_palette_main(LV_PALETTE_RED), 0);
    lv_meter_set_indicator_start_value(rpmMeter, indic, redline);
    lv_meter_set_indicator_end_value(rpmMeter, indic, max);

    /*Make the tick lines red at the end of the scale*/
    indic = lv_meter_add_scale_lines(rpmMeter, scale, lv_palette_main(LV_PALETTE_RED), lv_palette_main(LV_PALETTE_RED), false, 0);
    lv_meter_set_indicator_start_value(rpmMeter, indic, redline);
    lv_meter_set_indicator_end_value(rpmMeter, indic, max);

    /*Add a needle line indicator*/
    rpmIndicator = lv_meter_add_needle_line(rpmMeter, scale, 4, lv_palette_main(LV_PALETTE_GREY), -10);
/*
    // speed label
    speedLabel = lv_label_create(scr);
    lv_obj_add_style(speedLabel, &style, 0);
    lv_label_set_text(speedLabel, "-- km/h");
    lv_obj_align(speedLabel, LV_ALIGN_CENTER, 0, 50);

    // temperature label
    temperatureLabel = lv_label_create(scr);
    lv_obj_add_style(temperatureLabel, &style, 0);
    lv_label_set_text(temperatureLabel, "-- °C");
    lv_obj_align(temperatureLabel, LV_ALIGN_CENTER, 0, 100);
*/
    speedLabel = temperatureLabel = nullptr;
}

void UIManager::initPidTracking() {
    // the car is a 1999/2000 shitbox, so it of course doesn't speak standard OBD-II
    // the values are proprietary ('0x21' = KWP-2000 read at address) and undocumented unfortunately
    // so RPM is about the only thing we can reliably get...
    // AlfaOBD (possibly also MultiECUScan) is the only app that seems to understand every value from the ECU
    // https://www.alfaowner.com/threads/list-of-pids.305552/?post_id=8324898&nested_view=1#post-8324898
    trackedPids.emplace_back(std::make_unique<UIParsers::RPM>(rpmMeter, rpmIndicator));
    /*
    trackedPids.emplace_back(std::make_unique<UIParsers::SimpleLabel>("21311", speedLabel, [](const std::span<const uint8_t> data) {
        if (data.size() < 2) return std::string("-- km/h");
        // 16-bit value, big-endian // TODO CHECK
        const int raw = (data[0] << 8) | data[1];
        return std::to_string(raw / 128) + " km/h";
    }));
    trackedPids.emplace_back(std::make_unique<UIParsers::SimpleLabel>("21501", temperatureLabel, [](const std::span<const uint8_t> data) {
        if (data.empty()) return std::string("-- °C");
        return std::to_string(data[0] - 40) + " °C";
    }));
    */
}

void UIManager::pollRegisteredPids() const {
    for (auto& pid : trackedPids) {
        const std::string *lastCommand = nullptr;
        try {
            const auto& command = pid->getCommand();
            lastCommand = &command;

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
            pid->parse(dataSpan);

            const auto ptr = pid.get();

            xQueueSend(uiQueueHandle, &ptr, portMAX_DELAY);
        } catch (const std::exception& e) {
            ESP_LOGE(UI_TAG, "Error polling %s: %s", lastCommand ? lastCommand->c_str() : "", e.what());
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
