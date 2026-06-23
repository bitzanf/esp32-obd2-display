#include "RPM.hpp"

#include <stdexcept>

namespace UIParsers {
    RPM::RPM(lv_obj_t *meter, lv_meter_indicator_t *indicator) : command("21301"), meter(meter), indicator(indicator), rpmValue(0) {
        if (!meter || !indicator) {
            throw std::invalid_argument("Meter and indicator must not be null");
        }
        if (lv_obj_check_type(meter, &lv_meter_class) == false) {
            throw std::invalid_argument("Provided meter object is not of type lv_meter");
        }
    }

    void RPM::parse(const std::span<const uint8_t> data) {
        if (data.size() < 2) {
            rpmValue = 0;
            return;
        }

        rpmValue = (data[0] << 8) | data[1];
    }

    void RPM::updateUI() {
        lv_meter_set_indicator_value(meter, indicator, rpmValue);
    }
} // UIParsers