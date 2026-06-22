#ifndef ESP32_OBD2_DISPLAY_BLE_MANAGER_HPP
#define ESP32_OBD2_DISPLAY_BLE_MANAGER_HPP

#include <functional>
#include <stdexcept>
#include <string>

#include "Interfaces.hpp"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

#define BLE_TAG "BLE_MGR"

class BleException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class BleManager : public IBluetooth {
public:
    using NotificationCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using DisconnectCallback = std::function<void()>;

    // BLE characteristic UUIDs for the ELM327 adapter
    static constexpr auto DEFAULT_SERVICE_UUID = "000018f0-0000-1000-8000-00805f9b34fb";
    static constexpr auto DEFAULT_TX_CHAR_UUID = "00002af1-0000-1000-8000-00805f9b34fb";
    static constexpr auto DEFAULT_RX_CHAR_UUID = "00002af0-0000-1000-8000-00805f9b34fb";

    BleManager();
    ~BleManager() override = default;

    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;

    void registerNotificationCallback(const NotificationCallback &callback) override;
    void registerDisconnectCallback(const DisconnectCallback &callback) override;

    void writeData(const std::string& data) const override;

    void startScanAndConnect(const std::string& targetDeviceName = "IOS-Vlink");

    [[nodiscard]] bool isConnected() const override { return connHandle != BLE_HS_CONN_HANDLE_NONE; }

private:
    //NimBLE callbacks
    static int gapEventCallback(ble_gap_event *event, void *arg);

    static int gattDiscoveryCallback(
        uint16_t conn_handle,
        const ble_gatt_error *error,
        const ble_gatt_svc *service,
        void *arg
    );

    static int gattCharacteristicDiscoveryCallback(
        uint16_t conn_handle,
        const ble_gatt_error *error,
        const ble_gatt_chr *chr,
        void *arg
    );

    static void parseUuid(ble_uuid_any_t& uuid, const char* str);

    std::string deviceName;
    uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
    uint16_t txCharHandle = 0;
    uint16_t rxCharHandle = 0;

    ble_uuid_any_t serviceUuid;
    ble_uuid_any_t txCharUuid;
    ble_uuid_any_t rxCharUuid;

    NotificationCallback notificationCallback;
    DisconnectCallback disconnectCallback;
};



#endif //ESP32_OBD2_DISPLAY_BLE_MANAGER_HPP
