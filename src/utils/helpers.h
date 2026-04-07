#pragma once

namespace nexus::utils {

inline int clampPort(int port) {
    if (port < 1) {
        return 1;
    }
    if (port > 65535) {
        return 65535;
    }
    return port;
}

}  // namespace nexus::utils
