#include "MockImpl.hpp"

std::string MockObdDataProvider::sendCommand(const std::string &cmd, uint32_t timeout_ms) {
    // Return mock responses based on the command
    if (cmd == "ATZ") {
        return "ELM327 v1.5";
    } else if (cmd == "ATE0") {
        return "OK";
    } else if (cmd == "ATSP0") {
        return "OK";
    } else if (cmd.rfind("01 ", 0) == 0) { // PID request
        const uint8_t pid = std::stoi(cmd.substr(3), nullptr, 16);
        switch (pid) {
            case 0x0C: // Engine RPM
                return "410C2710"; // 2500 RPM
            case 0x0D: // Vehicle Speed
                return "410D28"; // 40 km/h
            case 0x05: // Coolant Temperature
                return "41055A"; // 50°C
            default:
                return "NODATA";
        }
    }
    return "UNKNOWN COMMAND";
}
