#pragma once

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <chrono>

#include "Usings.h"
#include "define.h"

// Packed structures for binary protocol efficiency
#pragma pack(push, 1)

//=============================================================================
// Message Types
//=============================================================================

enum class MessageType : uint8_t {
    // Inbound messages (client -> exchange)
    NEW_ORDER_REQUEST = 0x01,
    CANCEL_ORDER_REQUEST = 0x02,
    MODIFY_ORDER_REQUEST = 0x03,
    
    // Outbound messages (exchange -> client)
    ORDER_ACK = 0x10,
    ORDER_REJECT = 0x11,
    EXECUTION_REPORT = 0x12,
    CANCEL_ACK = 0x13,
    CANCEL_REJECT = 0x14,
    MODIFY_ACK = 0x15,
    MODIFY_REJECT = 0x16,
    
    // Market data messages (exchange -> multicast)
    TICK_UPDATE = 0x20,
    TRADE_UPDATE = 0x21,
    ORDERBOOK_SNAPSHOT = 0x22
};

//=============================================================================
// Reject Reason Codes
//=============================================================================

enum class RejectReason : uint8_t {
    NONE = 0x00,
    INVALID_PRICE = 0x01,
    INVALID_QUANTITY = 0x02,
    INVALID_SIDE = 0x03,
    INVALID_ORDER_TYPE = 0x04,
    ORDER_NOT_FOUND = 0x05,
    DUPLICATE_ORDER_ID = 0x06,
    SESSION_NOT_FOUND = 0x07,
    INVALID_MESSAGE = 0x08,
    INTERNAL_ERROR = 0xFF
};

//=============================================================================
// Message Header (common to all messages)
//=============================================================================

struct MessageHeader {
    MessageType type;
    uint16_t length;        // Total message length including header
    uint64_t timestamp;     // Nanoseconds since epoch
    
    MessageHeader() : type(MessageType::NEW_ORDER_REQUEST), length(0), timestamp(0) {}
    
    MessageHeader(MessageType t, uint16_t len)
        : type(t), length(len) {
        auto now = std::chrono::high_resolution_clock::now();
        timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
};

static_assert(sizeof(MessageHeader) == 11, "MessageHeader must be 11 bytes");

//=============================================================================
// Inbound Messages (Client -> Exchange)
//=============================================================================

struct NewOrderRequest {
    MessageHeader header;
    uint64_t clientOrderId;
    uint8_t side;           // BuySell enum
    uint8_t orderType;      // OrderType enum
    int32_t price;
    uint32_t quantity;
    
    NewOrderRequest() 
        : header(MessageType::NEW_ORDER_REQUEST, sizeof(NewOrderRequest))
        , clientOrderId(0), side(0), orderType(0), price(0), quantity(0) {}
};

struct CancelOrderRequest {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;       // Exchange-assigned order ID to cancel
    
    CancelOrderRequest()
        : header(MessageType::CANCEL_ORDER_REQUEST, sizeof(CancelOrderRequest))
        , clientOrderId(0), orderId(0) {}
};

struct ModifyOrderRequest {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;       // Exchange-assigned order ID to modify
    int32_t newPrice;
    uint32_t newQuantity;
    
    ModifyOrderRequest()
        : header(MessageType::MODIFY_ORDER_REQUEST, sizeof(ModifyOrderRequest))
        , clientOrderId(0), orderId(0), newPrice(0), newQuantity(0) {}
};

//=============================================================================
// Outbound Messages (Exchange -> Client)
//=============================================================================

struct OrderAck {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;       // Exchange-assigned order ID
    uint8_t status;         // OrderStatus enum
    
    OrderAck()
        : header(MessageType::ORDER_ACK, sizeof(OrderAck))
        , clientOrderId(0), orderId(0), status(0) {}
};

struct OrderReject {
    MessageHeader header;
    uint64_t clientOrderId;
    RejectReason reason;
    
    OrderReject()
        : header(MessageType::ORDER_REJECT, sizeof(OrderReject))
        , clientOrderId(0), reason(RejectReason::NONE) {}
};

struct ExecutionReport {
    MessageHeader header;
    uint64_t orderId;
    uint64_t tradeId;
    int32_t execPrice;
    uint32_t execQty;
    uint32_t leavesQty;     // Remaining quantity
    uint8_t side;           // BuySell enum
    
    ExecutionReport()
        : header(MessageType::EXECUTION_REPORT, sizeof(ExecutionReport))
        , orderId(0), tradeId(0), execPrice(0), execQty(0), leavesQty(0), side(0) {}
};

struct CancelAck {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;
    
    CancelAck()
        : header(MessageType::CANCEL_ACK, sizeof(CancelAck))
        , clientOrderId(0), orderId(0) {}
};

struct CancelReject {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;
    RejectReason reason;
    
    CancelReject()
        : header(MessageType::CANCEL_REJECT, sizeof(CancelReject))
        , clientOrderId(0), orderId(0), reason(RejectReason::NONE) {}
};

struct ModifyAck {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;
    int32_t newPrice;
    uint32_t newQuantity;
    
    ModifyAck()
        : header(MessageType::MODIFY_ACK, sizeof(ModifyAck))
        , clientOrderId(0), orderId(0), newPrice(0), newQuantity(0) {}
};

struct ModifyReject {
    MessageHeader header;
    uint64_t clientOrderId;
    uint64_t orderId;
    RejectReason reason;
    
    ModifyReject()
        : header(MessageType::MODIFY_REJECT, sizeof(ModifyReject))
        , clientOrderId(0), orderId(0), reason(RejectReason::NONE) {}
};

//=============================================================================
// Market Data Messages (Exchange -> UDP Multicast)
//=============================================================================

constexpr size_t SYMBOL_LENGTH = 8;

struct TickUpdate {
    MessageHeader header;
    char symbol[SYMBOL_LENGTH];
    int32_t lastPrice;
    uint32_t lastQty;
    int32_t bidPrice;
    int32_t askPrice;
    uint32_t bidQty;
    uint32_t askQty;
    
    TickUpdate()
        : header(MessageType::TICK_UPDATE, sizeof(TickUpdate))
        , lastPrice(0), lastQty(0)
        , bidPrice(0), askPrice(0), bidQty(0), askQty(0) {
        std::memset(symbol, 0, SYMBOL_LENGTH);
    }
};

struct TradeUpdate {
    MessageHeader header;
    char symbol[SYMBOL_LENGTH];
    uint64_t tradeId;
    int32_t price;
    uint32_t quantity;
    uint8_t aggressorSide;  // BuySell enum - side that initiated the trade
    
    TradeUpdate()
        : header(MessageType::TRADE_UPDATE, sizeof(TradeUpdate))
        , tradeId(0), price(0), quantity(0), aggressorSide(0) {
        std::memset(symbol, 0, SYMBOL_LENGTH);
    }
};

constexpr size_t MAX_DEPTH_LEVELS = 5;

struct PriceLevel {
    int32_t price;
    uint32_t quantity;
    uint32_t orderCount;
};

struct OrderbookSnapshot {
    MessageHeader header;
    char symbol[SYMBOL_LENGTH];
    uint8_t bidLevels;
    uint8_t askLevels;
    PriceLevel bids[MAX_DEPTH_LEVELS];
    PriceLevel asks[MAX_DEPTH_LEVELS];
    
    OrderbookSnapshot()
        : header(MessageType::ORDERBOOK_SNAPSHOT, sizeof(OrderbookSnapshot))
        , bidLevels(0), askLevels(0) {
        std::memset(symbol, 0, SYMBOL_LENGTH);
        std::memset(bids, 0, sizeof(bids));
        std::memset(asks, 0, sizeof(asks));
    }
};

#pragma pack(pop)

//=============================================================================
// Internal Message Wrapper (for queue communication)
//=============================================================================

struct InboundMessage {
    SessionId sessionId;
    MessageType type;
    union {
        NewOrderRequest newOrder;
        CancelOrderRequest cancelOrder;
        ModifyOrderRequest modifyOrder;
    };
    
    InboundMessage() : sessionId(0), type(MessageType::NEW_ORDER_REQUEST) {
        std::memset(&newOrder, 0, sizeof(newOrder));
    }
};

struct OutboundMessage {
    SessionId sessionId;
    MessageType type;
    union {
        OrderAck orderAck;
        OrderReject orderReject;
        ExecutionReport execReport;
        CancelAck cancelAck;
        CancelReject cancelReject;
        ModifyAck modifyAck;
        ModifyReject modifyReject;
    };
    
    OutboundMessage() : sessionId(0), type(MessageType::ORDER_ACK) {
        std::memset(&orderAck, 0, sizeof(orderAck));
    }
};

struct MarketDataMessage {
    MessageType type;
    union {
        TickUpdate tick;
        TradeUpdate trade;
        OrderbookSnapshot snapshot;
    };
    
    MarketDataMessage() : type(MessageType::TICK_UPDATE) {
        std::memset(&tick, 0, sizeof(tick));
    }
};

//=============================================================================
// Configuration Constants
//=============================================================================

namespace ExchangeConfig {
    constexpr uint16_t TCP_PORT = 12345;
    constexpr const char* MULTICAST_GROUP = "239.255.0.1";
    constexpr uint16_t MULTICAST_PORT = 12346;
    constexpr const char* DEFAULT_SYMBOL = "SYM1";
    constexpr uint32_t DEFAULT_EXPIRY = 20260131;
}

#endif // PROTOCOL_H
