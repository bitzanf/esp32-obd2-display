#ifndef ESP32_OBD2_DISPLAY_MOCK_IMPL_HPP
#define ESP32_OBD2_DISPLAY_MOCK_IMPL_HPP

#include <string>

#include "Interfaces.hpp"

class MockObdDataProvider : public IObd2 {
public:
    void initAdapter() override {} // no initialization needed
    ~MockObdDataProvider() override = default;

    [[nodiscard]] bool isConnected() const override { return true; }

    [[nodiscard]] std::string sendCommand(const std::string& cmd, uint32_t timeout_ms) override;
};

#endif //ESP32_OBD2_DISPLAY_MOCK_IMPL_HPP
