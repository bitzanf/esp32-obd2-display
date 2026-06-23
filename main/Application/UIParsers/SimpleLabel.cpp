#include "SimpleLabel.hpp"

#include <stdexcept>
#include <utility>

namespace UIParsers {
    SimpleLabel::SimpleLabel(std::string command, lv_obj_t *label, std::function<std::string(std::span<const uint8_t> data)> processor) : command(std::move(command)), processor(std::move(processor)), label(label) {
        if (!label) {
            throw std::invalid_argument("Label must not be null");
        }
        if (lv_obj_check_type(label, &lv_label_class) == false) {
            throw std::invalid_argument("Provided label object is not of type lv_label");
        }
    }

    void SimpleLabel::parse(const std::span<const uint8_t> data) {
        text = processor(data);
    }

    void SimpleLabel::updateUI() {
        lv_label_set_text(label, text.c_str());
    }
} // UIParsers