/**
 * @file SessionManager.h
 * @brief Client session management and message routing.
 * 
 * Manages client sessions, tracks active orders per session, and
 * routes messages between the network layer and matching engine.
 */

#pragma once

#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>

#include "Usings.h"
#include "Protocol.h"
#include "LockFreeQueue.h"

/**
 * @struct SessionInfo
 * @brief Information about a connected client session.
 */
struct SessionInfo {
    SessionId sessionId;                        ///< Unique session identifier
    std::string connectionId;                  ///< Connection identifier
    bool isActive;                              ///< Whether session is active
    std::unordered_set<OrderId> activeOrders;   ///< Orders owned by this session
    
    SessionInfo() : sessionId(INVALID_SESSION_ID), isActive(false) {}
    SessionInfo(SessionId id, const std::string& identity)
        : sessionId(id), connectionId(identity), isActive(true) {}
};

/**
 * @class SessionManager
 * @brief Manages client sessions and routes messages.
 * 
 * The SessionManager:
 * - Creates and destroys client sessions
 * - Maps connection identifiers to session IDs
 * - Tracks which orders belong to which session
 * - Routes inbound messages to the matching engine
 * - Routes outbound messages to the correct client
 * 
 * @par Thread Safety
 * All public methods are thread-safe (protected by mutex).
 */
class SessionManager {
public:
    using InboundQueue = MPSCQueue<InboundMessage, INBOUND_QUEUE_SIZE>;
    using OutboundQueue = SPSCQueue<OutboundMessage, OUTBOUND_QUEUE_SIZE>;
    using DisconnectCallback = std::function<void(SessionId, const std::unordered_set<OrderId>&)>;
    
    /**
     * @brief Construct a SessionManager.
     * @param inboundQueue Queue for incoming messages to matching engine.
     * @param outboundQueue Queue for outgoing messages to clients.
     */
    SessionManager(InboundQueue& inboundQueue, OutboundQueue& outboundQueue);
    ~SessionManager() = default;
    
    /**
     * @brief Create a new session for a client.
     * @param connectionId Client connection identifier.
     * @return Assigned session ID.
     */
    SessionId createSession(const std::string& connectionId);
    
    /**
     * @brief Destroy a session and trigger disconnect callback.
     * @param sessionId Session to destroy.
     */
    void destroySession(SessionId sessionId);
    
    /**
     * @brief Check if a session is active.
     * @param sessionId Session to check.
     * @return true if session exists and is active.
     */
    bool isSessionActive(SessionId sessionId) const;
    
    /**
     * @brief Get connection identifier for a session.
     * @param sessionId Session ID.
     * @return Connection identifier string, empty if not found.
     */
    std::string getIdentity(SessionId sessionId) const;
    
    // Order tracking
    void addOrderToSession(SessionId sessionId, OrderId orderId);
    void removeOrderFromSession(SessionId sessionId, OrderId orderId);
    
    // Message routing
    bool routeToInbound(SessionId sessionId, const NewOrderRequest& request);
    bool routeToInbound(SessionId sessionId, const CancelOrderRequest& request);
    bool routeToInbound(SessionId sessionId, const ModifyOrderRequest& request);
    
    // Get pending outbound messages for a session
    std::optional<OutboundMessage> getOutboundMessage();
    
    // Set callback for when a session disconnects (to cancel its orders)
    void setDisconnectCallback(DisconnectCallback callback);
    
    // Statistics
    size_t getActiveSessionCount() const;
    
private:
    InboundQueue& inboundQueue_;
    OutboundQueue& outboundQueue_;
    
    mutable std::mutex sessionMutex_;
    std::unordered_map<SessionId, SessionInfo> sessions_;
    std::unordered_map<std::string, SessionId> identityToSession_;  // Reverse lookup
    
    std::atomic<SessionId> nextSessionId_{1};
    
    DisconnectCallback disconnectCallback_;
};

//=============================================================================
// Implementation
//=============================================================================

inline SessionManager::SessionManager(InboundQueue& inboundQueue, OutboundQueue& outboundQueue)
    : inboundQueue_(inboundQueue)
    , outboundQueue_(outboundQueue)
{
}

inline SessionId SessionManager::createSession(const std::string& connectionId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    // Check if this identity already has a session
    auto existingIt = identityToSession_.find(connectionId);
    if (existingIt != identityToSession_.end()) {
        return existingIt->second;  // Return existing session
    }
    
    SessionId sessionId = nextSessionId_.fetch_add(1);
    sessions_.emplace(sessionId, SessionInfo(sessionId, connectionId));
    identityToSession_[connectionId] = sessionId;
    
    return sessionId;
}

inline void SessionManager::destroySession(SessionId sessionId) {
    std::unordered_set<OrderId> ordersToCancel;
    
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return;
        }
        
        SessionInfo& session = it->second;
        session.isActive = false;
        ordersToCancel = std::move(session.activeOrders);
        
        identityToSession_.erase(session.connectionId);
        sessions_.erase(it);
    }
    
    // Notify about disconnection (outside lock to avoid deadlock)
    if (disconnectCallback_ && !ordersToCancel.empty()) {
        disconnectCallback_(sessionId, ordersToCancel);
    }
}

inline bool SessionManager::isSessionActive(SessionId sessionId) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto it = sessions_.find(sessionId);
    return it != sessions_.end() && it->second.isActive;
}

inline std::string SessionManager::getIdentity(SessionId sessionId) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return "";
    }
    return it->second.connectionId;
}

inline void SessionManager::addOrderToSession(SessionId sessionId, OrderId orderId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second.activeOrders.insert(orderId);
    }
}

inline void SessionManager::removeOrderFromSession(SessionId sessionId, OrderId orderId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second.activeOrders.erase(orderId);
    }
}

inline bool SessionManager::routeToInbound(SessionId sessionId, const NewOrderRequest& request) {
    InboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::NEW_ORDER_REQUEST;
    msg.newOrder = request;
    return inboundQueue_.push(std::move(msg));
}

inline bool SessionManager::routeToInbound(SessionId sessionId, const CancelOrderRequest& request) {
    InboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::CANCEL_ORDER_REQUEST;
    msg.cancelOrder = request;
    return inboundQueue_.push(std::move(msg));
}

inline bool SessionManager::routeToInbound(SessionId sessionId, const ModifyOrderRequest& request) {
    InboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::MODIFY_ORDER_REQUEST;
    msg.modifyOrder = request;
    return inboundQueue_.push(std::move(msg));
}

inline std::optional<OutboundMessage> SessionManager::getOutboundMessage() {
    return outboundQueue_.pop();
}

inline void SessionManager::setDisconnectCallback(DisconnectCallback callback) {
    disconnectCallback_ = std::move(callback);
}

inline size_t SessionManager::getActiveSessionCount() const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    return sessions_.size();
}

#endif // SESSIONMANAGER_H
