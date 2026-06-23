#ifndef ESP32_OBD2_DISPLAY_SIMPLELABEL_HPP
#define ESP32_OBD2_DISPLAY_SIMPLELABEL_HPP

#include <lvgl.h>
#include <functional>
#include <string>
#include <span>
#include <cstdint>

#include "Interfaces.hpp"

namespace UIParsers {

class SimpleLabel : public IObdPid {
public:
    SimpleLabel(std::string command, lv_obj_t* label, std::function<std::string(std::span<const uint8_t> data)> processor);
    ~SimpleLabel() override = default;

    [[nodiscard]] const std::string& getCommand() const override { return command; }

    void parse(std::span<const uint8_t> data) override;

    void updateUI() override;
private:
    std::string command;
    std::string text;
    std::function<std::string(std::span<const uint8_t> data)> processor;
    lv_obj_t* label;
};

} // UIParsers

#endif //ESP32_OBD2_DISPLAY_SIMPLELABEL_HPP
