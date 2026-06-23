#ifndef ESP32_OBD2_DISPLAY_INTERFACES_HPP
#define ESP32_OBD2_DISPLAY_INTERFACES_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <span>

class IBluetooth {
public:
    using NotificationCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using DisconnectCallback = std::function<void()>;

    virtual ~IBluetooth() = default;

    virtual void writeData(const std::string& data) const = 0;
    [[nodiscard]] virtual bool isConnected() const = 0;

    virtual void registerNotificationCallback(const NotificationCallback &callback) = 0;
    virtual void registerDisconnectCallback(const DisconnectCallback &callback) = 0;
};

class IObd2 {
public:
    virtual ~IObd2() = default;

    virtual void initAdapter() = 0;
    [[nodiscard]] virtual bool isConnected() const = 0;

    [[nodiscard]] virtual std::string sendCommand(const std::string& cmd, uint32_t timeout_ms) = 0;
};

class IObdPid {
public:
    virtual ~IObdPid() = default;

    [[nodiscard]] virtual const std::string& getCommand() const = 0;
    virtual void parse(std::span<const uint8_t> data) = 0;
    virtual void updateUI() = 0;
};

#endif //ESP32_OBD2_DISPLAY_INTERFACES_HPP
