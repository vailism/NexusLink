#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
using discovery_socket_t = SOCKET;
constexpr discovery_socket_t kInvalidDiscoverySocket = INVALID_SOCKET;
#else
using discovery_socket_t = int;
constexpr discovery_socket_t kInvalidDiscoverySocket = -1;
#endif

namespace nexus::network {

constexpr int kDiscoveryPort = 50000;
constexpr const char* kDiscoveryPrefix = "NEXUSLINK_SERVER:";

class DiscoveryBroadcaster {
public:
    DiscoveryBroadcaster() = default;
    ~DiscoveryBroadcaster();

    DiscoveryBroadcaster(const DiscoveryBroadcaster&) = delete;
    DiscoveryBroadcaster& operator=(const DiscoveryBroadcaster&) = delete;

    bool start(int serverPort);
    void stop();

private:
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    discovery_socket_t socket_ = kInvalidDiscoverySocket;
};

class DiscoveryListener {
public:
    DiscoveryListener() = default;
    ~DiscoveryListener();

    DiscoveryListener(const DiscoveryListener&) = delete;
    DiscoveryListener& operator=(const DiscoveryListener&) = delete;

    bool start(const std::function<void(const std::string&, int)>& onServerDiscovered);
    void stop();

private:
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex mutex_;
    discovery_socket_t socket_ = kInvalidDiscoverySocket;
};

}  // namespace nexus::network
