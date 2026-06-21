#include "Obd2Manager.hpp"

#include "BleManager.hpp"

Obd2Manager::Obd2Manager(BleManager &bleManager) : ble(&bleManager) {
    ble->registerNotificationCallback([this](const std::vector<uint8_t> &data) {
        handleBleNotification(data);
    });
    ble->registerDisconnectCallback([this] {
        handleDisconnect();
    });
}

void Obd2Manager::initAdapter() {
    ESP_LOGI(OBD_TAG, "Initializing OBD-II adapter...");

    std::string response = sendCommand("ATZ");
    ESP_LOGI(OBD_TAG, "Adapter reset response: %s", response.c_str());
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for adapter to reset

    // disable echo
    sendCommand("ATE0");

    // automatic protocol select
    sendCommand("ATSP0");

    ESP_LOGI(OBD_TAG, "Adapter initialized.");
}

bool Obd2Manager::isConnected() const {
    return ble->isConnected();
}

std::string Obd2Manager::sendCommand(const std::string &cmd, const uint32_t timeout_ms) {
    if (!ble->isConnected()) {
        throw Obd2Exception("Not connected to OBD-II adapter");
    }

    {
        std::lock_guard lock(bufferMutex);
        rxBuffer.clear();
        responseReceived = false;
    }

    std::string formatted = cmd;
    if (formatted.empty() || formatted.back() != '\r') {
        formatted += '\r';
    }

    ble->writeData(formatted);

    std::unique_lock bufferLock(bufferMutex);
    const auto success = responseCondition.wait_for(bufferLock, std::chrono::milliseconds(timeout_ms), [this] {
        return responseReceived.load();
    });

    if (!ble->isConnected()) {
        throw Obd2Exception("Not connected to OBD-II adapter");
    }

    if (!success) throw Obd2Exception("OBD-II adapter response timeout (" + cmd + ")");

    auto rawResult = rxBuffer;
    cleanResponse(rawResult);

    if (rawResult.find("NODATA") != std::string::npos) {
        throw Obd2Exception("No OBD2 data received from ECU for command: " + cmd);
    }

    if (rawResult.find("ERROR") != std::string::npos) {
        throw Obd2Exception("OBD2 HW error for command: " + cmd);
    }

    return rawResult;
}

std::vector<uint8_t> Obd2Manager::hexStringToBytes(const std::string &hexString) {
    std::vector<uint8_t> bytes;
    if (hexString.length() % 2 != 0) {
        throw Obd2Exception("Hex string must have an even length");
    }

    bytes.reserve(hexString.length() / 2);
    for (size_t i = 0; i < hexString.length(); i += 2) {
        uint8_t byte = std::stoi(hexString.substr(i, 2), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

void Obd2Manager::handleBleNotification(const std::vector<uint8_t> &data) {
    std::lock_guard lock(bufferMutex);

    rxBuffer.append(data.begin(), data.end());

    if (!rxBuffer.empty() && rxBuffer.find('>') != std::string::npos) {
        responseReceived = true;
        responseCondition.notify_one();
    }
}

void Obd2Manager::handleDisconnect() {
    std::lock_guard lock(bufferMutex);
    responseReceived = true;
    responseCondition.notify_all();
}

void Obd2Manager::cleanResponse(std::string &response) {
    std::erase_if(response, [](const char c) {
        return c == '\r' || c == '\n' || c == '>' || c == ' ';
    });
}
