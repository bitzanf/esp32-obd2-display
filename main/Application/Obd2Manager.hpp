#ifndef ESP32_OBD2_DISPLAY_OBD2_MANAGER_HPP
#define ESP32_OBD2_DISPLAY_OBD2_MANAGER_HPP

#include <mutex>
#include <stdexcept>
#include <vector>
#include <condition_variable>

#include "Interfaces.hpp"

#define OBD_TAG "OBD_MGR"

class Obd2Exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Obd2Manager : public IObd2 {
public:
    constexpr static uint32_t DEFAULT_TIMEOUT_MS = 2000;

    explicit Obd2Manager(IBluetooth* bleManager);
    ~Obd2Manager() override = default;

    Obd2Manager(const Obd2Manager&) = delete;
    Obd2Manager& operator=(const Obd2Manager&) = delete;

    void initAdapter() override;

    [[nodiscard]] bool isConnected() const override;

    [[nodiscard]] std::string sendCommand(const std::string& cmd, uint32_t timeout_ms) override;

    static std::vector<uint8_t> hexStringToBytes(const std::string& hexString);
private:
    IBluetooth* ble;

    std::string rxBuffer;
    std::mutex bufferMutex;
    std::condition_variable responseCondition;
    std::atomic_bool responseReceived = false;

    void handleBleNotification(const std::vector<uint8_t>& data);
    void handleDisconnect();

    static void cleanResponse(std::string& response);
};



#endif //ESP32_OBD2_DISPLAY_OBD2_MANAGER_HPP
