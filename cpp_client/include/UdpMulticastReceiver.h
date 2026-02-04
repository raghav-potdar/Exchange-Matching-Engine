/**
 * @file UdpMulticastReceiver.h
 * @brief UDP multicast receiver for exchange market data.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <atomic>

#include "../include/Protocol.h"

class UdpMulticastReceiver {
public:
    using Handler = std::function<void(const MessageHeader& header, const void* payload, size_t len)>;

    UdpMulticastReceiver(std::string multicastGroup, uint16_t port);
    ~UdpMulticastReceiver();

    bool start(Handler handler);
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    void loop();

    std::string multicastGroup_;
    uint16_t port_{0};

    int socketFd_{-1};
    std::atomic<bool> running_{false};
    Handler handler_{};
};

