#include "connection.h"

#include <cstdint>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <vector>

#include "../security/encryption.h"

namespace nexus::network {

Connection::Connection(TcpSocket socket) : socket_(std::move(socket)) {}

bool Connection::sendMessage(const std::string& message) const {
    const std::string encrypted = security::xorCipher(message, security::defaultKey());

    const uint32_t size = static_cast<uint32_t>(encrypted.size());
    const uint32_t netSize = htonl(size);

    if (socket_.sendAll(reinterpret_cast<const char*>(&netSize), static_cast<int>(sizeof(netSize))) <= 0) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    return socket_.sendAll(encrypted.data(), static_cast<int>(size)) > 0;
}

bool Connection::receiveMessage(std::string& outMessage) const {
    uint32_t netSize = 0;
    if (!receiveExact(reinterpret_cast<char*>(&netSize), static_cast<int>(sizeof(netSize)))) {
        return false;
    }

    const uint32_t size = ntohl(netSize);
    if (size == 0) {
        outMessage.clear();
        return true;
    }

    std::vector<char> buffer(size);
    if (!receiveExact(buffer.data(), static_cast<int>(size))) {
        return false;
    }

    const std::string encrypted(buffer.begin(), buffer.end());
    outMessage = security::xorCipher(encrypted, security::defaultKey());
    return true;
}

bool Connection::valid() const {
    return socket_.valid();
}

bool Connection::receiveExact(char* buffer, int length) const {
    int total = 0;
    while (total < length) {
        int received = socket_.recvSome(buffer + total, length - total);
        if (received <= 0) {
            return false;
        }
        total += received;
    }
    return true;
}

}  // namespace nexus::network
