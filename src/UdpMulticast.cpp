#include "UdpMulticast.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

UdpMulticast::UdpMulticast(const std::string& multicastGroup, uint16_t port)
    : multicastGroup_(multicastGroup)
    , port_(port)
{
}

UdpMulticast::~UdpMulticast() {
    shutdown();
}

bool UdpMulticast::initialize() {
    // Create UDP socket
    socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Set TTL for multicast packets
    unsigned char ttl = 1;  // Local network only
    if (setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        std::cerr << "Failed to set multicast TTL: " << strerror(errno) << std::endl;
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }
    
    // Enable loopback so clients on the same host can receive multicast.
    // If you want to disable local loopback in the future, make this configurable.
    unsigned char loopback = 1;
    if (setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0) {
        std::cerr << "Failed to set multicast loopback: " << strerror(errno) << std::endl;
        // Non-fatal, continue
    }
    
    std::cout << "UDP Multicast publisher initialized: " << multicastGroup_ << ":" << port_ << std::endl;
    return true;
}

void UdpMulticast::shutdown() {
    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }
    std::cout << "UDP Multicast publisher shutdown" << std::endl;
}

bool UdpMulticast::publish(const void* data, size_t length) {
    if (socketFd_ < 0) {
        return false;
    }
    
    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port_);
    
    if (inet_pton(AF_INET, multicastGroup_.c_str(), &destAddr.sin_addr) <= 0) {
        std::cerr << "Invalid multicast address: " << multicastGroup_ << std::endl;
        return false;
    }
    
    ssize_t sent = sendto(socketFd_, data, length, 0,
                          reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr));
    
    if (sent < 0) {
        std::cerr << "sendto failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (static_cast<size_t>(sent) != length) {
        std::cerr << "Partial send: " << sent << " of " << length << " bytes" << std::endl;
        return false;
    }
    
    messagesSent_.fetch_add(1, std::memory_order_relaxed);
    bytesSent_.fetch_add(length, std::memory_order_relaxed);
    
    return true;
}

template<typename PublishType>
bool UdpMulticast::publishMessage(const PublishType& message) {
    return publish(&message, sizeof(message));
}
