/**
 * @file MatchingEngine.h
 * @brief Order matching engine with session tracking and market data publishing.
 * 
 * The MatchingEngine wraps the Orderbook and provides:
 * - Message-based order processing from inbound queue
 * - Response generation to outbound queue
 * - Session-to-order tracking for disconnect handling
 * - Market data publishing to feed queue
 */

#pragma once

#ifndef MATCHINGENGINE_H
#define MATCHINGENGINE_H

#include <atomic>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

#include "Orderbook.h"
#include "Protocol.h"
#include "LockFreeQueue.h"
#include "FeedHandler.h"

/**
 * @class MatchingEngine
 * @brief Processes orders from inbound queue and generates responses.
 * 
 * The MatchingEngine runs in its own thread, continuously processing
 * messages from the inbound queue. It handles:
 * - New order requests (validation, matching, booking)
 * - Cancel requests (order removal)
 * - Modify requests (price/quantity changes)
 * 
 * All operations are single-threaded for deterministic behavior.
 * 
 * @par Order Lifecycle
 * 1. NewOrderRequest received from inbound queue
 * 2. Validate order parameters
 * 3. Attempt matching against resting orders
 * 4. Book remaining quantity (for LIMIT orders)
 * 5. Send OrderAck/OrderReject to outbound queue
 * 6. Send ExecutionReport for any fills
 * 7. Publish market data updates
 */
class MatchingEngine {
public:
    using InboundQueue = MPSCQueue<InboundMessage, INBOUND_QUEUE_SIZE>;
    using OutboundQueue = SPSCQueue<OutboundMessage, OUTBOUND_QUEUE_SIZE>;
    using FeedQueue = SPSCQueue<MarketDataMessage, FEED_QUEUE_SIZE>;
    
    /**
     * @brief Construct a new MatchingEngine.
     * @param inboundQueue Queue for incoming order messages.
     * @param outboundQueue Queue for outgoing response messages.
     * @param feedQueue Queue for market data messages.
     * @param symbol Trading symbol this engine handles.
     */
    MatchingEngine(InboundQueue& inboundQueue, 
                   OutboundQueue& outboundQueue,
                   FeedQueue& feedQueue,
                   const std::string& symbol);
    
    /**
     * @brief Destructor. Stops matching thread if running.
     */
    ~MatchingEngine();
    
    /**
     * @brief Start the matching engine thread.
     * @return true if started successfully, false if already running.
     */
    bool start();
    
    /**
     * @brief Stop the matching engine gracefully.
     * 
     * Drains remaining messages from the inbound queue before stopping.
     */
    void stop();
    
    /**
     * @brief Check if the matching engine is running.
     * @return true if running, false otherwise.
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Cancel all orders belonging to a session.
     * 
     * Called when a client disconnects to clean up their orders.
     * 
     * @param sessionId Session identifier.
     * @param orderIds Set of order IDs to cancel.
     */
    void cancelSessionOrders(SessionId sessionId, const std::unordered_set<OrderId>& orderIds);
    
    /**
     * @brief Get total number of orders processed.
     * @return Count of orders processed since startup.
     */
    uint64_t getOrdersProcessed() const { return ordersProcessed_.load(); }
    
    /**
     * @brief Get total number of trades executed.
     * @return Count of trades executed since startup.
     */
    uint64_t getTradesExecuted() const { return tradesExecuted_.load(); }
    
private:
    /** @brief Main matching loop running in dedicated thread. */
    void matchingLoop();
    
    /** @brief Process a single inbound message. */
    void processInboundMessage(const InboundMessage& msg);
    
    /** @brief Handle new order request. */
    void handleNewOrder(SessionId sessionId, const NewOrderRequest& request);
    
    /** @brief Handle cancel order request. */
    void handleCancelOrder(SessionId sessionId, const CancelOrderRequest& request);
    
    /** @brief Handle modify order request. */
    void handleModifyOrder(SessionId sessionId, const ModifyOrderRequest& request);
    
    // Response senders
    void sendOrderAck(SessionId sessionId, ClientOrderId clientOrderId, OrderId orderId, OrderStatus status);
    void sendOrderReject(SessionId sessionId, ClientOrderId clientOrderId, RejectReason reason);
    void sendExecutionReport(SessionId sessionId, OrderId orderId, Price execPrice, 
                             Quantity execQty, Quantity leavesQty, BuySell side);
    void sendCancelAck(SessionId sessionId, ClientOrderId clientOrderId, OrderId orderId);
    void sendCancelReject(SessionId sessionId, ClientOrderId clientOrderId, OrderId orderId, RejectReason reason);
    void sendModifyAck(SessionId sessionId, ClientOrderId clientOrderId, OrderId orderId, Price newPrice, Quantity newQty);
    void sendModifyReject(SessionId sessionId, ClientOrderId clientOrderId, OrderId orderId, RejectReason reason);
    
    /** @brief Publish trade to market data feed. */
    void publishTradeUpdate(const Orderbook::Trade& trade, BuySell aggressorSide);
    
    /** @brief Publish tick update with best bid/ask. */
    void publishTickUpdate();
    
    /** @brief Get current best bid and ask from orderbook. */
    void getBestBidAsk(Price& bidPrice, Quantity& bidQty, Price& askPrice, Quantity& askQty);
    
private:
    InboundQueue& inboundQueue_;
    OutboundQueue& outboundQueue_;
    FeedQueue& feedQueue_;
    
    Orderbook orderbook_;
    std::string symbol_;
    
    std::atomic<bool> running_{false};
    std::thread matchingThread_;
    
    std::atomic<OrderId> nextOrderId_{1};
    std::atomic<TradeId> nextTradeId_{1};
    
    std::unordered_map<SessionId, std::unordered_set<OrderId>> sessionOrders_;
    std::unordered_map<OrderId, SessionId> orderToSession_;
    std::unordered_map<OrderId, ClientOrderId> orderToClientOrderId_;
    
    Price lastTradePrice_{0};
    Quantity lastTradeQty_{0};
    
    std::atomic<uint64_t> ordersProcessed_{0};
    std::atomic<uint64_t> tradesExecuted_{0};
};

#endif // MATCHINGENGINE_H
