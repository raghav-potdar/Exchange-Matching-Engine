/**
 * @file UdpMulticast.h
 * @brief UDP multicast publisher for market data distribution.
 * 
 * Provides low-latency market data distribution to multiple
 * clients simultaneously using UDP multicast.
 */

#pragma once

#ifndef UDPMULTICAST_H
#define UDPMULTICAST_H

#include <string>
#include <atomic>

#include "Protocol.h"

/**
 * @class UdpMulticast
 * @brief UDP multicast publisher for market data.
 * 
 * Sends market data messages to a multicast group address.
 * Clients join the multicast group to receive data.
 * 
 * @par Multicast Addressing
 * Uses IPv4 multicast addresses in the range 239.0.0.0 - 239.255.255.255
 * (administratively scoped addresses).
 * 
 * @par Example Usage
 * @code
 * UdpMulticast publisher("239.255.0.1", 12346);
 * if (publisher.initialize()) {
 *     TickUpdate tick = {...};
 *     publisher.publishMessage(tick);
 * }
 * @endcode
 */
class UdpMulticast {
public:
    /**
     * @brief Construct a UDP multicast publisher.
     * @param multicastGroup Multicast group address (e.g., "239.255.0.1").
     * @param port UDP port number.
     */
    UdpMulticast(const std::string& multicastGroup, uint16_t port);
    
    /**
     * @brief Destructor. Closes socket if open.
     */
    ~UdpMulticast();
    
    /**
     * @brief Initialize the multicast socket.
     * @return true if successful, false on error.
     */
    bool initialize();
    
    /**
     * @brief Shutdown and close the socket.
     */
    void shutdown();

    /**
     * @brief Publish an arbitrary message.
     * @param message Message to send.
     * @return true if sent successfully.
     */
    template<typename PublishType>
    bool publishMessage(const PublishType& message);

    /**
     * @brief Get count of messages sent.
     * @return Total messages sent since initialization.
     */
    uint64_t getMessagesSent() const { return messagesSent_.load(); }
    
    /**
     * @brief Get count of bytes sent.
     * @return Total bytes sent since initialization.
     */
    uint64_t getBytesSent() const { return bytesSent_.load(); }
    
private:
    std::string multicastGroup_;
    uint16_t port_;
    int socketFd_{-1};
    
    std::atomic<uint64_t> messagesSent_{0};
    std::atomic<uint64_t> bytesSent_{0};

    /**
     * @brief Publish raw data to multicast group.
     * @param data Pointer to data buffer.
     * @param length Length of data in bytes.
     * @return true if sent successfully.
     */
    bool publish(const void* data, size_t length);
};

#endif // UDPMULTICAST_H
