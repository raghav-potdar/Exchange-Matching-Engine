#include "UdpMulticastReceiver.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>

UdpMulticastReceiver::UdpMulticastReceiver(std::string multicastGroup, uint16_t port)
    : multicastGroup_(std::move(multicastGroup))
    , port_(port) {}

UdpMulticastReceiver::~UdpMulticastReceiver() {
    stop();
}

bool UdpMulticastReceiver::start(Handler handler) {
    if (running_.exchange(true)) {
        return false;
    }
    handler_ = std::move(handler);

    socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        std::cerr << "UDP recv socket() failed: " << std::strerror(errno) << "\n";
        running_.store(false);
        return false;
    }

    int reuse = 1;
    if (setsockopt(socketFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "UDP recv setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << "\n";
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "UDP recv bind() failed: " << std::strerror(errno) << "\n";
        ::close(socketFd_);
        socketFd_ = -1;
        running_.store(false);
        return false;
    }

    ip_mreq mreq{};
    if (inet_pton(AF_INET, multicastGroup_.c_str(), &mreq.imr_multiaddr) <= 0) {
        std::cerr << "Invalid multicast group: " << multicastGroup_ << "\n";
        ::close(socketFd_);
        socketFd_ = -1;
        running_.store(false);
        return false;
    }
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(socketFd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::cerr << "UDP recv IP_ADD_MEMBERSHIP failed: " << std::strerror(errno) << "\n";
        ::close(socketFd_);
        socketFd_ = -1;
        running_.store(false);
        return false;
    }

    std::thread([this] { loop(); }).detach();
    return true;
}

void UdpMulticastReceiver::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
}

void UdpMulticastReceiver::loop() {
    constexpr size_t MAX_DATAGRAM = 2048;
    alignas(8) unsigned char buffer[MAX_DATAGRAM];

    while (running_.load()) {
        ssize_t n = ::recvfrom(socketFd_, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n <= 0) {
            if (!running_.load()) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            // If socket is closed from another thread, recvfrom will error.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (static_cast<size_t>(n) < sizeof(MessageHeader)) {
            continue;
        }

        const auto* header = reinterpret_cast<const MessageHeader*>(buffer);
        size_t declaredLen = header->length;
        size_t actualLen = static_cast<size_t>(n);
        size_t useLen = (declaredLen > 0 && declaredLen <= actualLen) ? declaredLen : actualLen;

        if (handler_) {
            handler_(*header, buffer, useLen);
        }
    }
}

