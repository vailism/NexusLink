#include "discovery.h"

#include <chrono>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using udp_socket_t = SOCKET;
constexpr udp_socket_t kInvalidUdpSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using udp_socket_t = int;
constexpr udp_socket_t kInvalidUdpSocket = -1;
#endif

namespace nexus::network {

namespace {

void closeUdp(udp_socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    ::close(sock);
#endif
}

bool setRecvTimeout1s(udp_socket_t sock) {
#ifdef _WIN32
    const DWORD timeoutMs = 1000;
    return ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) ==
           0;
#else
    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    return ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool parseDiscoveryPayload(const std::string& payload, int& outPort) {
    if (payload.rfind(kDiscoveryPrefix, 0) != 0) {
        return false;
    }

    const std::string portPart = payload.substr(std::strlen(kDiscoveryPrefix));
    if (portPart.empty()) {
        return false;
    }

    try {
        const int port = std::stoi(portPart);
        if (port < 1 || port > 65535) {
            return false;
        }
        outPort = port;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

DiscoveryBroadcaster::~DiscoveryBroadcaster() {
    stop();
}

bool DiscoveryBroadcaster::start(int serverPort) {
    if (running_.load()) {
        return true;
    }

    if (serverPort < 1 || serverPort > 65535) {
        return false;
    }

    running_.store(true);
    worker_ = std::thread([this, serverPort]() {
        udp_socket_t sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == kInvalidUdpSocket) {
            running_.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            socket_ = sock;
        }

        int enable = 1;
        if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enable), sizeof(enable)) !=
            0) {
            closeUdp(sock);
            running_.store(false);
            return;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(kDiscoveryPort));
        addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

        const std::string payload = std::string(kDiscoveryPrefix) + std::to_string(serverPort);

        while (running_.load()) {
            ::sendto(sock,
                     payload.c_str(),
                     static_cast<int>(payload.size()),
                     0,
                     reinterpret_cast<const sockaddr*>(&addr),
                     static_cast<int>(sizeof(addr)));

            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(2), [&]() { return !running_.load(); });
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (socket_ != kInvalidDiscoverySocket) {
                closeUdp(socket_);
                socket_ = kInvalidDiscoverySocket;
            }
        }
    });

    return true;
}

void DiscoveryBroadcaster::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    cv_.notify_all();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (socket_ != kInvalidDiscoverySocket) {
            closeUdp(socket_);
            socket_ = kInvalidDiscoverySocket;
        }
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

DiscoveryListener::~DiscoveryListener() {
    stop();
}

bool DiscoveryListener::start(const std::function<void(const std::string&, int)>& onServerDiscovered) {
    if (running_.load()) {
        return true;
    }

    running_.store(true);
    worker_ = std::thread([this, onServerDiscovered]() {
        udp_socket_t sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == kInvalidUdpSocket) {
            running_.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            socket_ = sock;
        }

        int reuse = 1;
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        setRecvTimeout1s(sock);

        sockaddr_in bindAddr{};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(static_cast<uint16_t>(kDiscoveryPort));
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (::bind(sock, reinterpret_cast<const sockaddr*>(&bindAddr), static_cast<int>(sizeof(bindAddr))) != 0) {
            closeUdp(sock);
            running_.store(false);
            return;
        }

        char buffer[512] = {0};
        while (running_.load()) {
            sockaddr_in fromAddr{};
#ifdef _WIN32
            int fromLen = static_cast<int>(sizeof(fromAddr));
#else
            socklen_t fromLen = static_cast<socklen_t>(sizeof(fromAddr));
#endif
            const int recvLen = ::recvfrom(sock,
                                           buffer,
                                           static_cast<int>(sizeof(buffer) - 1),
                                           0,
                                           reinterpret_cast<sockaddr*>(&fromAddr),
                                           &fromLen);

            if (recvLen <= 0) {
                continue;
            }

            buffer[recvLen] = '\0';
            int serverPort = 0;
            if (!parseDiscoveryPayload(buffer, serverPort)) {
                continue;
            }

            char ipBuffer[INET_ADDRSTRLEN] = {0};
            const char* ipRes =
                ::inet_ntop(AF_INET, reinterpret_cast<const void*>(&fromAddr.sin_addr), ipBuffer, INET_ADDRSTRLEN);
            if (ipRes == nullptr) {
                continue;
            }

            onServerDiscovered(ipBuffer, serverPort);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (socket_ != kInvalidDiscoverySocket) {
                closeUdp(socket_);
                socket_ = kInvalidDiscoverySocket;
            }
        }
    });

    return true;
}

void DiscoveryListener::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (socket_ != kInvalidDiscoverySocket) {
            closeUdp(socket_);
            socket_ = kInvalidDiscoverySocket;
        }
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

}  // namespace nexus::network
