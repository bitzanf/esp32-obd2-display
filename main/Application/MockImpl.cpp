#include "MockImpl.hpp"

std::string MockObdDataProvider::sendCommand(const std::string &cmd, uint32_t timeout_ms) {
    if (cmd.rfind("AT", 0) == 0) {
        return "OK";
    } else if (cmd.rfind("21", 0) == 0) {
        const auto pid = std::stoi(cmd.substr(2, 2), nullptr, 16);
        switch (pid) {
            case 0x30: // RPM
                return "613002AE";
            default:
                return "7F00";
        }
    }

    return "UNKNOWN COMMAND";
}
