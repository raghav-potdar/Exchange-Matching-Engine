/**
 * @file ZmqServer.h
 * @brief ZeroMQ ROUTER socket server for client connections.
 * 
 * Provides a ZeroMQ-based server using the ROUTER/DEALER pattern.
 * Clients connect with DEALER sockets and the server automatically
 * tracks client identity for routing responses.
 */

#pragma once

#ifndef ZMQSERVER_H
#define ZMQSERVER_H

#include <zmq.hpp>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Usings.h"
#include "Protocol.h"
#include "SessionManager.h"

/**
 * @class ZmqServer
 * @brief ZeroMQ ROUTER socket server for client order entry.
 * 
 * The ZmqServer uses ZeroMQ's ROUTER socket pattern which:
 * - Automatically assigns identity to each connected client
 * - Allows routing messages to specific clients by identity
 * - Supports asynchronous bidirectional messaging
 * 
 * @par Message Format
 * ROUTER socket message frames:
 * - Frame 1: Client identity (assigned by ZMQ or set by client)
 * - Frame 2: Empty delimiter frame
 * - Frame 3: Binary message payload
 * 
 * @par Threading
 * Runs in its own thread, processing both incoming messages
 * from clients and outbound responses from the matching engine.
 * 
 * @par Example Client Connection (Python)
 * @code
 * import zmq
 * context = zmq.Context()
 * socket = context.socket(zmq.DEALER)
 * socket.setsockopt(zmq.IDENTITY, b"my-client-id")
 * socket.connect("tcp://localhost:12345")
 * @endcode
 */
class ZmqServer {
public:
    /**
     * @brief Construct a new ZmqServer.
     * @param port TCP port to bind the ROUTER socket.
     * @param sessionManager Reference to SessionManager for session creation/routing.
     */
    ZmqServer(uint16_t port, SessionManager& sessionManager);
    
    /**
     * @brief Destructor. Stops server and closes sockets.
     */
    ~ZmqServer();
    
    /**
     * @brief Start the ZMQ server.
     * 
     * Creates ZMQ context, binds ROUTER socket, and starts
     * the event loop thread.
     * 
     * @return true if started successfully, false on error.
     */
    bool start();
    
    /**
     * @brief Stop the ZMQ server gracefully.
     * 
     * Stops the event loop, disconnects all clients, and
     * closes the ZMQ socket and context.
     */
    void stop();
    
    /**
     * @brief Check if the server is running.
     * @return true if running, false otherwise.
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Send a message to a specific client.
     * @param identity Client's ZMQ identity.
     * @param data Pointer to message data.
     * @param length Length of message in bytes.
     * @return true if sent successfully, false on error.
     */
    bool sendToClient(const std::string& identity, const void* data, size_t length);
    
private:
    /** @brief Main event loop running in dedicated thread. */
    void eventLoop();
    
    /** @brief Process incoming messages from clients. */
    void processIncoming();
    
    /** @brief Process and send outbound messages from queue. */
    void processOutbound();
    
    /**
     * @brief Parse and route a message from a client.
     * @param identity Client's ZMQ identity.
     * @param msg ZMQ message containing binary payload.
     * @return true if message was valid and routed, false otherwise.
     */
    bool parseAndRouteMessage(const std::string& identity, const zmq::message_t& msg);
    
    /**
     * @brief Handle client disconnection.
     * @param identity Client's ZMQ identity.
     */
    void handleClientDisconnect(const std::string& identity);
    
private:
    uint16_t port_;
    SessionManager& sessionManager_;
    
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    
    std::unordered_map<std::string, SessionId> identityToSession_;
    std::unordered_map<SessionId, std::string> sessionToIdentity_;
    
    static constexpr int POLL_TIMEOUT_MS = 100;  ///< Polling timeout in milliseconds
};

#endif // ZMQSERVER_H
