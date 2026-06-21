#include "BleManager.hpp"

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <services/gap/ble_svc_gap.h>

extern "C" {
    void ble_store_config_init() {}
}

BleManager::BleManager() { // NOLINT(*-pro-type-member-init)
    serviceUuid = {};
    txCharUuid = {};
    rxCharUuid = {};

    parseUuid(serviceUuid, DEFAULT_SERVICE_UUID);
    parseUuid(txCharUuid, DEFAULT_TX_CHAR_UUID);
    parseUuid(rxCharUuid, DEFAULT_RX_CHAR_UUID);

    if (const auto rc = nimble_port_init(); rc != 0)
        throw BleException("Failed to initialize NimBLE port: " + std::to_string(rc));

    if (const auto rc = ble_svc_gap_device_name_set("ESP32-S3-OBD2-DISPLAY"); rc != 0)
        throw BleException("Failed to set device name set: " + std::to_string(rc));

    nimble_port_freertos_init([](void*) {
        ESP_LOGI(BLE_TAG, "NimBLE host thread started");
        nimble_port_run();
        nimble_port_freertos_deinit();
    });
}

void BleManager::registerNotificationCallback(const NotificationCallback &callback) {
    notificationCallback = callback;
}

void BleManager::registerDisconnectCallback(const DisconnectCallback &callback) {
    disconnectCallback = callback;
}

void BleManager::startScanAndConnect(const std::string &targetDeviceName) {
    deviceName = targetDeviceName;

    ble_gap_disc_params discParams = {};
    discParams.passive = 0;
    discParams.filter_duplicates = 1;
    discParams.itvl = 0x0010;
    discParams.window = 0x0010;

    if (const auto rc = ble_gap_disc(
        BLE_OWN_ADDR_PUBLIC,
        BLE_HS_FOREVER,
        &discParams,
        gapEventCallback,
        this
    ); rc != 0)
        throw BleException("Failed to start scanning: " + std::to_string(rc));

    ESP_LOGI(BLE_TAG, "Started scanning for devices with name: %s", deviceName.c_str());
}

void BleManager::writeData(const std::string &data) const {
    if (!isConnected() || txCharHandle == 0) {
        throw BleException("Cannot write data. Not connected or TX characteristic not found.");
    }

    if (const auto rc = ble_gattc_write_flat(
        connHandle,
        txCharHandle,
        data.data(),
        data.size(),
        nullptr,
        nullptr
    ); rc != 0) {
        throw BleException("Failed to write data: " + std::to_string(rc));
    }
}

// ReSharper disable once CppDFAConstantFunctionResult
int BleManager::gapEventCallback(ble_gap_event *event, void *arg) {
    auto* self = static_cast<BleManager*>(arg);

    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            ble_hs_adv_fields fields{};
            if (const auto rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data); rc != 0) {
                ESP_LOGE(BLE_TAG, "Failed to parse advertisement fields: %d", rc);
                return 0;
            }

            if (fields.name != nullptr) {
                std::string advName(reinterpret_cast<const char*>(fields.name), fields.name_len);
                ESP_LOGI(BLE_TAG, "Discovered device: %s", advName.c_str());

                if (advName == self->deviceName) {
                    ESP_LOGI(BLE_TAG, "Found target device. Connecting...", advName.c_str());
                    ble_gap_disc_cancel();

                    if (const auto rc = ble_gap_connect(
                        BLE_OWN_ADDR_PUBLIC,
                        &event->disc.addr,
                        30000,
                        nullptr,
                        gapEventCallback,
                        self
                    ); rc != 0) {
                        ESP_LOGE(BLE_TAG, "Failed to initiate connection: %d", rc);
                    }
                }
            }
            break;
        }

        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                self->connHandle = event->connect.conn_handle;
                ESP_LOGI(BLE_TAG, "Connected to device. Handle: 0x%04x", self->connHandle);

                ble_gattc_disc_svc_by_uuid(self->connHandle, &self->serviceUuid.u, gattDiscoveryCallback, self);
            } else {
                ESP_LOGW(BLE_TAG, "Connection failed: %d", event->connect.status);
                self->connHandle = BLE_HS_CONN_HANDLE_NONE;
            }
            break;
        }

        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGW(BLE_TAG, "Disconnected from device. Reason: %d", event->disconnect.reason);
            self->connHandle = BLE_HS_CONN_HANDLE_NONE;

            if (self->disconnectCallback) {
                self->disconnectCallback();
            }

            ESP_LOGI(BLE_TAG, "Restarting scan...");
            try {
                self->startScanAndConnect(self->deviceName);
            } catch (const std::exception& e) {
                ESP_LOGE(BLE_TAG, "Failed to restart scan: %s", e.what());
            }
            break;
        }

        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (event->notify_rx.conn_handle == self->connHandle && event->notify_rx.attr_handle == self->rxCharHandle) {
                std::vector<uint8_t> data;
                auto* om = event->notify_rx.om;
                while (om != nullptr) {
                    data.insert(data.end(), om->om_data, om->om_data + om->om_len);
                    om = SLIST_NEXT(om, om_next);
                }

                if (self->notificationCallback) {
                    self->notificationCallback(data);
                }
            }
            break;
        }
    }
    return 0;
}

int BleManager::gattDiscoveryCallback(
    uint16_t conn_handle,
    const ble_gatt_error *error,
    const ble_gatt_svc *service,
    void *arg
) {
    auto* self = static_cast<BleManager*>(arg);
    if (error->status == 0) {
        ESP_LOGI(BLE_TAG, "GATT Discovery Successful. Discovering characteristics...");
        if (const auto rc = ble_gattc_disc_all_chrs(
            conn_handle,
            service->start_handle,
            service->end_handle,
            gattCharacteristicDiscoveryCallback,
            self
        ); rc != 0) {
            ESP_LOGE(BLE_TAG, "Failed to discover characteristics. %d", rc);
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(BLE_TAG, "GATT Discovery Complete.");
    } else {
        ESP_LOGE(BLE_TAG, "GATT Discovery Failed. Status: %d", error->status);
    }
    return 0;
}

int BleManager::gattCharacteristicDiscoveryCallback(
    uint16_t conn_handle,
    const ble_gatt_error *error,
    const ble_gatt_chr *chr,
    void *arg
) {
    auto* self = static_cast<BleManager*>(arg);
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &self->txCharUuid.u) == 0) {
            self->txCharHandle = chr->val_handle;
            ESP_LOGI(BLE_TAG, "TX Characteristic found. Handle: 0x%04x", self->txCharHandle);
        } else if (ble_uuid_cmp(&chr->uuid.u, &self->rxCharUuid.u) == 0) {
            self->rxCharHandle = chr->val_handle;
            ESP_LOGI(BLE_TAG, "RX Characteristic found. Handle: 0x%04x", self->rxCharHandle);

            // subscribe to notifications on the RX characteristic
            uint16_t ccc_handle = chr->val_handle + 1;
            uint16_t val = 0x0001;
            ble_gattc_write_flat(
                conn_handle,
                ccc_handle,
                &val,
                sizeof(val),
                nullptr,
                nullptr
            );
            ESP_LOGI(BLE_TAG, "Subscribed to RX notifications.");
        }
    } else {
        ESP_LOGE(BLE_TAG, "GATT Discovery Failed. Status: %d", error->status);
    }
    return 0;
}

void BleManager::parseUuid(ble_uuid_any_t &uuid, const char *str) {
    if (const auto ret = ble_uuid_from_str(&uuid, str); ret != 0) {
        throw BleException("Failed to parse UUID: " + std::string(str));
    }
}
