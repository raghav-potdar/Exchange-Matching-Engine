/**
 * @file Order.h
 * @brief Order class representing a single order in the orderbook.
 */

#ifndef ORDER_H
#define ORDER_H

#include "define.h"
#include "Usings.h"
#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>

/**
 * @class Order
 * @brief Represents a single order in the orderbook.
 * 
 * Contains all order attributes including price, quantity, timestamps,
 * order type, side, and session tracking information.
 */
class Order
{
private:
    Price price_;                   ///< Order price (0 for MARKET orders)
    Quantity quantity_;             ///< Remaining quantity
    Timestamp timestamp_;           ///< Order creation timestamp
    OrderId orderId_;               ///< Exchange-assigned order ID
    OrderType orderType_;           ///< Order type (LIMIT, MARKET, IOC)
    BuySell buySell_;               ///< Order side (BUY or SELL)
    OrderStatus orderStatus_;       ///< Current order status
    SessionId sessionId_;           ///< Session that owns this order
    ClientOrderId clientOrderId_;   ///< Client-assigned order ID

public:
    /**
     * @brief Construct an Order (legacy, without session tracking).
     * @param price Order price.
     * @param quantity Order quantity.
     * @param timestamp Order timestamp.
     * @param orderId Exchange-assigned order ID.
     * @param orderType Order type.
     * @param buySell Order side.
     * @param orderStatus Initial order status.
     */
    Order(Price price, Quantity quantity, Timestamp timestamp, OrderId orderId, 
          OrderType orderType, BuySell buySell, OrderStatus orderStatus)
        : price_(price), quantity_(quantity), timestamp_(timestamp), orderId_(orderId)
        , orderType_(orderType), buySell_(buySell), orderStatus_(orderStatus)
        , sessionId_(INVALID_SESSION_ID), clientOrderId_(0) {}
    
    /**
     * @brief Construct an Order with session tracking.
     * @param price Order price.
     * @param quantity Order quantity.
     * @param timestamp Order timestamp.
     * @param orderId Exchange-assigned order ID.
     * @param orderType Order type.
     * @param buySell Order side.
     * @param orderStatus Initial order status.
     * @param sessionId Session that owns this order.
     * @param clientOrderId Client-assigned order ID.
     */
    Order(Price price, Quantity quantity, Timestamp timestamp, OrderId orderId, 
          OrderType orderType, BuySell buySell, OrderStatus orderStatus,
          SessionId sessionId, ClientOrderId clientOrderId)
        : price_(price), quantity_(quantity), timestamp_(timestamp), orderId_(orderId)
        , orderType_(orderType), buySell_(buySell), orderStatus_(orderStatus)
        , sessionId_(sessionId), clientOrderId_(clientOrderId) {}
    
    ~Order() = default;

    /** @name Getters */
    ///@{
    Price getPrice() const { return price_; }
    Quantity getQuantity() const { return quantity_; }
    Timestamp getTimestamp() const { return timestamp_; }
    OrderId getOrderId() const { return orderId_; }
    OrderType getOrderType() const { return orderType_; }
    BuySell getBuySell() const { return buySell_; }
    OrderStatus getOrderStatus() const { return orderStatus_; }
    SessionId getSessionId() const { return sessionId_; }
    ClientOrderId getClientOrderId() const { return clientOrderId_; }
    ///@}
    
    /** @name Setters */
    ///@{
    void setPrice(Price price) { price_ = price; }
    void setQuantity(Quantity quantity) { quantity_ = quantity; }
    void setOrderStatus(OrderStatus status) { orderStatus_ = status; }
    void setSessionId(SessionId sessionId) { sessionId_ = sessionId; }
    void setClientOrderId(ClientOrderId clientOrderId) { clientOrderId_ = clientOrderId; }
    ///@}
    
    /** @name Quantity Operations */
    ///@{
    
    /**
     * @brief Increase order quantity.
     * @param quantity Amount to add.
     */
    void increaseQuantity(Quantity quantity) { quantity_ += quantity; }
    
    /**
     * @brief Reduce order quantity by delta.
     * @param delta Amount to reduce.
     * @return Actual amount reduced (may be less if quantity < delta).
     */
    Quantity reduceQuantity(Quantity delta)
    {
        Quantity executed = std::min(quantity_, delta);
        quantity_ -= executed;
        return executed;
    }
    
    /**
     * @brief Check if order is fully filled.
     * @return true if quantity is zero.
     */
    bool isFilled() const { return quantity_ == 0; }
    ///@}
};

using OrderPointer = std::shared_ptr<Order>;  ///< Shared pointer to Order
using OrderPointers = std::list<OrderPointer>;  ///< List of order pointers

#endif // ORDER_H