#ifndef ESP32_OBD2_DISPLAY_RPM_HPP
#define ESP32_OBD2_DISPLAY_RPM_HPP

#include <lvgl.h>
#include <span>

#include "Interfaces.hpp"

namespace UIParsers {

class RPM : public IObdPid {
public:
    RPM(lv_obj_t* meter, lv_meter_indicator_t* indicator);
    ~RPM() override = default;

    [[nodiscard]] const std::string& getCommand() const override { return command; }

    void parse(std::span<const uint8_t> data) override;

    void updateUI() override;
private:
    std::string command;
    lv_obj_t* meter;
    lv_meter_indicator_t* indicator;
    int32_t rpmValue;
};

} // UIParsers

#endif //ESP32_OBD2_DISPLAY_RPM_HPP
