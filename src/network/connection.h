#pragma once

#include <string>

#include "socket.h"

namespace nexus::network {

class Connection {
public:
    explicit Connection(TcpSocket socket);

    bool sendMessage(const std::string& message) const;
    bool receiveMessage(std::string& outMessage) const;
    bool valid() const;

private:
    TcpSocket socket_;
    bool receiveExact(char* buffer, int length) const;
};

}  // namespace nexus::network
