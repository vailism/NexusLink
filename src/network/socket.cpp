#include "socket.h"

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nexus::network {

namespace {
using addr_len_t =
#ifdef _WIN32
    int;
#else
    socklen_t;
#endif

void closeNative(socket_handle_t handle) {
#ifdef _WIN32
    closesocket(handle);
#else
    ::close(handle);
#endif
}
}  // namespace

bool initializeSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

TcpSocket::TcpSocket() : handle_(kInvalidSocket) {}

TcpSocket::TcpSocket(socket_handle_t handle) : handle_(handle) {}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = kInvalidSocket;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = kInvalidSocket;
    }
    return *this;
}

TcpSocket::~TcpSocket() {
    close();
}

bool TcpSocket::create() {
    close();
    handle_ = ::socket(AF_INET, SOCK_STREAM, 0);
    return valid();
}

bool TcpSocket::bindAndListen(int port, int backlog) {
    if (!valid()) {
        return false;
    }

    int opt = 1;
    if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return false;
    }

    return ::listen(handle_, backlog) == 0;
}

TcpSocket TcpSocket::acceptClient() const {
    if (!valid()) {
        return TcpSocket();
    }

    sockaddr_in clientAddr{};
    addr_len_t len = static_cast<addr_len_t>(sizeof(clientAddr));
    socket_handle_t clientHandle = ::accept(handle_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    return TcpSocket(clientHandle);
}

bool TcpSocket::connectTo(const std::string& host, int port) {
    if (!valid()) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        return false;
    }

    return ::connect(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

int TcpSocket::sendAll(const char* data, int length) const {
    if (!valid() || data == nullptr || length <= 0) {
        return -1;
    }

    int totalSent = 0;
    while (totalSent < length) {
        const int sent = static_cast<int>(::send(handle_, data + totalSent, length - totalSent, 0));
        if (sent <= 0) {
            return -1;
        }
        totalSent += sent;
    }
    return totalSent;
}

int TcpSocket::recvSome(char* buffer, int length) const {
    if (!valid() || buffer == nullptr || length <= 0) {
        return -1;
    }
    return static_cast<int>(::recv(handle_, buffer, length, 0));
}

void TcpSocket::close() {
    if (valid()) {
        closeNative(handle_);
        handle_ = kInvalidSocket;
    }
}

bool TcpSocket::valid() const {
    return handle_ != kInvalidSocket;
}

socket_handle_t TcpSocket::nativeHandle() const {
    return handle_;
}

}  // namespace nexus::network
