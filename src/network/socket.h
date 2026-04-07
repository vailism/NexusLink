#pragma once

#include <string>

#ifdef _WIN32
#include <winsock2.h>
using socket_handle_t = SOCKET;
constexpr socket_handle_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_handle_t = int;
constexpr socket_handle_t kInvalidSocket = -1;
#endif

namespace nexus::network {

bool initializeSockets();
void cleanupSockets();

class TcpSocket {
public:
    TcpSocket();
    explicit TcpSocket(socket_handle_t handle);
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    ~TcpSocket();

    bool create();
    bool bindAndListen(int port, int backlog = 16);
    TcpSocket acceptClient() const;
    bool connectTo(const std::string& host, int port);
    int sendAll(const char* data, int length) const;
    int recvSome(char* buffer, int length) const;
    void close();
    bool valid() const;
    socket_handle_t nativeHandle() const;

private:
    socket_handle_t handle_;
};

}  // namespace nexus::network
