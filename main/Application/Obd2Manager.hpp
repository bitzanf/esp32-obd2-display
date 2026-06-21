#ifndef ESP32_OBD2_DISPLAY_OBD2_MANAGER_HPP
#define ESP32_OBD2_DISPLAY_OBD2_MANAGER_HPP

#include <mutex>
#include <stdexcept>
#include <vector>
#include <condition_variable>

#define OBD_TAG "OBD_MGR"

class BleManager;

class Obd2Exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Obd2Manager {
public:
    explicit Obd2Manager(BleManager& bleManager);
    ~Obd2Manager() = default;

    Obd2Manager(const Obd2Manager&) = delete;
    Obd2Manager& operator=(const Obd2Manager&) = delete;

    void initAdapter();

    [[nodiscard]] bool isConnected() const;

    std::string sendCommand(const std::string& cmd, uint32_t timeout_ms = 2000);

    static std::vector<uint8_t> hexStringToBytes(const std::string& hexString);
private:
    BleManager* ble;

    std::string rxBuffer;
    std::mutex bufferMutex;
    std::condition_variable responseCondition;
    std::atomic_bool responseReceived = false;

    void handleBleNotification(const std::vector<uint8_t>& data);
    void handleDisconnect();

    static void cleanResponse(std::string& response);
};



#endif //ESP32_OBD2_DISPLAY_OBD2_MANAGER_HPP
