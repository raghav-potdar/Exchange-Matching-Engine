#include "MatchingEngine.h"

#include <iostream>
#include <cstring>

MatchingEngine::MatchingEngine(InboundQueue& inboundQueue,
                               OutboundQueue& outboundQueue,
                               FeedQueue& feedQueue,
                               const std::string& symbol)
    : inboundQueue_(inboundQueue)
    , outboundQueue_(outboundQueue)
    , feedQueue_(feedQueue)
    , symbol_(symbol)
{
}

MatchingEngine::~MatchingEngine() {
    stop();
}

bool MatchingEngine::start() {
    if (running_.exchange(true)) {
        return false;  // Already running
    }
    
    matchingThread_ = std::thread(&MatchingEngine::matchingLoop, this);
    
    std::cout << "Matching Engine started for symbol: " << symbol_ << std::endl;
    return true;
}

void MatchingEngine::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    if (matchingThread_.joinable()) {
        matchingThread_.join();
    }
    
    std::cout << "Matching Engine stopped (orders: " << ordersProcessed_.load()
              << ", trades: " << tradesExecuted_.load() << ")" << std::endl;
}

void MatchingEngine::matchingLoop() {
    while (running_.load()) {
        // Process all available inbound messages
        bool processed = false;
        while (auto msgOpt = inboundQueue_.pop()) {
            processInboundMessage(*msgOpt);
            processed = true;
        }
        
        // If no messages, sleep briefly to avoid busy-waiting
        if (!processed) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    
    // Drain remaining messages
    while (auto msgOpt = inboundQueue_.pop()) {
        processInboundMessage(*msgOpt);
    }
}

void MatchingEngine::processInboundMessage(const InboundMessage& msg) {
    switch (msg.type) {
        case MessageType::NEW_ORDER_REQUEST:
            handleNewOrder(msg.sessionId, msg.newOrder);
            break;
        case MessageType::CANCEL_ORDER_REQUEST:
            handleCancelOrder(msg.sessionId, msg.cancelOrder);
            break;
        case MessageType::MODIFY_ORDER_REQUEST:
            handleModifyOrder(msg.sessionId, msg.modifyOrder);
            break;
        default:
            break;
    }
}

void MatchingEngine::handleNewOrder(SessionId sessionId, const NewOrderRequest& request) {
    ordersProcessed_.fetch_add(1, std::memory_order_relaxed);
    
    // Validate request
    BuySell side = static_cast<BuySell>(request.side);
    if (side != BuySell::BUY && side != BuySell::SELL) {
        sendOrderReject(sessionId, request.clientOrderId, RejectReason::INVALID_SIDE);
        return;
    }
    
    OrderType orderType = static_cast<OrderType>(request.orderType);
    if (orderType != OrderType::LIMIT && orderType != OrderType::MARKET && orderType != OrderType::IOC) {
        sendOrderReject(sessionId, request.clientOrderId, RejectReason::INVALID_ORDER_TYPE);
        return;
    }
    
    if (request.quantity == 0) {
        sendOrderReject(sessionId, request.clientOrderId, RejectReason::INVALID_QUANTITY);
        return;
    }
    
    // For LIMIT orders, price must be positive
    if (orderType == OrderType::LIMIT && request.price <= 0) {
        sendOrderReject(sessionId, request.clientOrderId, RejectReason::INVALID_PRICE);
        return;
    }
    
    // Generate order ID
    OrderId orderId = nextOrderId_.fetch_add(1);
    
    // Get current timestamp
    auto now = std::chrono::high_resolution_clock::now();
    Timestamp timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    // Create order
    auto order = std::make_shared<Order>(
        request.price,
        request.quantity,
        timestamp,
        orderId,
        orderType,
        side,
        OrderStatus::NEW,
        sessionId,
        request.clientOrderId
    );
    
    // Track order
    sessionOrders_[sessionId].insert(orderId);
    orderToSession_[orderId] = sessionId;
    orderToClientOrderId_[orderId] = request.clientOrderId;
    
    // Add to orderbook (this may generate trades)
    orderbook_.AddOrder(order);
    
    // Process any trades that occurred
    const auto& trades = orderbook_.GetRecentTrades();
    for (const auto& trade : trades) {
        tradesExecuted_.fetch_add(1, std::memory_order_relaxed);
        
        // Determine aggressor side (incoming order)
        BuySell aggressorSide = side;
        
        // Update last trade info
        lastTradePrice_ = trade.price;
        lastTradeQty_ = trade.quantity;
        
        // Publish trade update
        publishTradeUpdate(trade, aggressorSide);
        
        // Send execution reports
        // For the aggressor (incoming order)
        sendExecutionReport(sessionId, orderId, trade.price, trade.quantity,
                           order->getQuantity(), side);
        
        // For the passive (resting order)
        OrderId passiveOrderId = (side == BuySell::BUY) ? trade.sellOrderId : trade.buyOrderId;
        auto passiveIt = orderToSession_.find(passiveOrderId);
        if (passiveIt != orderToSession_.end()) {
            SessionId passiveSessionId = passiveIt->second;
            // We don't have the resting order's remaining quantity here,
            // but the matching already happened in the orderbook
            sendExecutionReport(passiveSessionId, passiveOrderId, trade.price, trade.quantity,
                               0, (side == BuySell::BUY) ? BuySell::SELL : BuySell::BUY);
        }
    }
    
    // Send order acknowledgment
    sendOrderAck(sessionId, request.clientOrderId, orderId, order->getOrderStatus());
    
    // If order was fully filled or cancelled (IOC/MARKET), clean up tracking
    if (order->isFilled() || order->getOrderStatus() == OrderStatus::CANCELLED) {
        sessionOrders_[sessionId].erase(orderId);
        orderToSession_.erase(orderId);
        orderToClientOrderId_.erase(orderId);
    }
    
    // Publish tick update with best bid/ask
    publishTickUpdate();
}

void MatchingEngine::handleCancelOrder(SessionId sessionId, const CancelOrderRequest& request) {
    // Verify the order belongs to this session
    auto it = orderToSession_.find(request.orderId);
    if (it == orderToSession_.end()) {
        sendCancelReject(sessionId, request.clientOrderId, request.orderId, RejectReason::ORDER_NOT_FOUND);
        return;
    }
    
    if (it->second != sessionId && sessionId != DUMMY_SESSION_ID) {
        // Order doesn't belong to this session
        sendCancelReject(sessionId, request.clientOrderId, request.orderId, RejectReason::ORDER_NOT_FOUND);
        return;
    }
    
    // Cancel the order
    orderbook_.CancelOrder(request.orderId);
    
    // Clean up tracking
    SessionId orderSessionId = it->second;
    sessionOrders_[orderSessionId].erase(request.orderId);
    orderToSession_.erase(request.orderId);
    orderToClientOrderId_.erase(request.orderId);
    
    // Send cancel ack
    sendCancelAck(sessionId, request.clientOrderId, request.orderId);
    
    // Publish tick update
    publishTickUpdate();
}

void MatchingEngine::handleModifyOrder(SessionId sessionId, const ModifyOrderRequest& request) {
    // Verify the order belongs to this session
    auto it = orderToSession_.find(request.orderId);
    if (it == orderToSession_.end()) {
        sendModifyReject(sessionId, request.clientOrderId, request.orderId, RejectReason::ORDER_NOT_FOUND);
        return;
    }
    
    if (it->second != sessionId && sessionId != DUMMY_SESSION_ID) {
        sendModifyReject(sessionId, request.clientOrderId, request.orderId, RejectReason::ORDER_NOT_FOUND);
        return;
    }
    
    // Validate new parameters
    if (request.newPrice <= 0) {
        sendModifyReject(sessionId, request.clientOrderId, request.orderId, RejectReason::INVALID_PRICE);
        return;
    }
    
    if (request.newQuantity == 0) {
        sendModifyReject(sessionId, request.clientOrderId, request.orderId, RejectReason::INVALID_QUANTITY);
        return;
    }
    
    // Modify the order
    orderbook_.ModifyOrder(request.orderId, request.newPrice, request.newQuantity);
    
    // Process any trades from the modify (price change could cause matching)
    const auto& trades = orderbook_.GetRecentTrades();
    for (const auto& trade : trades) {
        tradesExecuted_.fetch_add(1, std::memory_order_relaxed);
        
        lastTradePrice_ = trade.price;
        lastTradeQty_ = trade.quantity;
        
        // Determine which side was the aggressor (the modified order)
        BuySell aggressorSide = (trade.buyOrderId == request.orderId) ? BuySell::BUY : BuySell::SELL;
        publishTradeUpdate(trade, aggressorSide);
    }
    
    // Send modify ack
    sendModifyAck(sessionId, request.clientOrderId, request.orderId, request.newPrice, request.newQuantity);
    
    // Publish tick update
    publishTickUpdate();
}

void MatchingEngine::cancelSessionOrders(SessionId sessionId, const std::unordered_set<OrderId>& orderIds) {
    for (OrderId orderId : orderIds) {
        orderbook_.CancelOrder(orderId);
        orderToSession_.erase(orderId);
        orderToClientOrderId_.erase(orderId);
    }
    sessionOrders_.erase(sessionId);
    
    // Publish tick update after cancellations
    if (!orderIds.empty()) {
        publishTickUpdate();
    }
}

void MatchingEngine::sendOrderAck(SessionId sessionId, ClientOrderId clientOrderId, 
                                   OrderId orderId, OrderStatus status) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::ORDER_ACK;
    
    OrderAck& ack = msg.orderAck;
    ack.header = MessageHeader(MessageType::ORDER_ACK, sizeof(OrderAck));
    ack.clientOrderId = clientOrderId;
    ack.orderId = orderId;
    ack.status = static_cast<uint8_t>(status);
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::sendOrderReject(SessionId sessionId, ClientOrderId clientOrderId, RejectReason reason) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::ORDER_REJECT;
    
    OrderReject& reject = msg.orderReject;
    reject.header = MessageHeader(MessageType::ORDER_REJECT, sizeof(OrderReject));
    reject.clientOrderId = clientOrderId;
    reject.reason = reason;
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::sendExecutionReport(SessionId sessionId, OrderId orderId, 
                                          Price execPrice, Quantity execQty,
                                          Quantity leavesQty, BuySell side) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::EXECUTION_REPORT;
    
    ExecutionReport& report = msg.execReport;
    report.header = MessageHeader(MessageType::EXECUTION_REPORT, sizeof(ExecutionReport));
    report.orderId = orderId;
    report.tradeId = nextTradeId_.fetch_add(1);
    report.execPrice = execPrice;
    report.execQty = execQty;
    report.leavesQty = leavesQty;
    report.side = static_cast<uint8_t>(side);
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::sendCancelAck(SessionId sessionId, ClientOrderId clientOrderId, OrderId orderId) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::CANCEL_ACK;
    
    CancelAck& ack = msg.cancelAck;
    ack.header = MessageHeader(MessageType::CANCEL_ACK, sizeof(CancelAck));
    ack.clientOrderId = clientOrderId;
    ack.orderId = orderId;
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::sendCancelReject(SessionId sessionId, ClientOrderId clientOrderId, 
                                       OrderId orderId, RejectReason reason) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::CANCEL_REJECT;
    
    CancelReject& reject = msg.cancelReject;
    reject.header = MessageHeader(MessageType::CANCEL_REJECT, sizeof(CancelReject));
    reject.clientOrderId = clientOrderId;
    reject.orderId = orderId;
    reject.reason = reason;
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::sendModifyAck(SessionId sessionId, ClientOrderId clientOrderId, 
                                    OrderId orderId, Price newPrice, Quantity newQty) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::MODIFY_ACK;
    
    ModifyAck& ack = msg.modifyAck;
    ack.header = MessageHeader(MessageType::MODIFY_ACK, sizeof(ModifyAck));
    ack.clientOrderId = clientOrderId;
    ack.orderId = orderId;
    ack.newPrice = newPrice;
    ack.newQuantity = newQty;
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::sendModifyReject(SessionId sessionId, ClientOrderId clientOrderId, 
                                       OrderId orderId, RejectReason reason) {
    OutboundMessage msg;
    msg.sessionId = sessionId;
    msg.type = MessageType::MODIFY_REJECT;
    
    ModifyReject& reject = msg.modifyReject;
    reject.header = MessageHeader(MessageType::MODIFY_REJECT, sizeof(ModifyReject));
    reject.clientOrderId = clientOrderId;
    reject.orderId = orderId;
    reject.reason = reason;
    
    outboundQueue_.push(std::move(msg));
}

void MatchingEngine::publishTradeUpdate(const Orderbook::Trade& trade, BuySell aggressorSide) {
    MarketDataMessage msg;
    msg.type = MessageType::TRADE_UPDATE;
    
    TradeUpdate& update = msg.trade;
    update.header = MessageHeader(MessageType::TRADE_UPDATE, sizeof(TradeUpdate));
    
    std::strncpy(update.symbol, symbol_.c_str(), SYMBOL_LENGTH - 1);
    update.symbol[SYMBOL_LENGTH - 1] = '\0';
    
    update.tradeId = nextTradeId_.load();  // Use current trade ID
    update.price = trade.price;
    update.quantity = trade.quantity;
    update.aggressorSide = static_cast<uint8_t>(aggressorSide);
    
    feedQueue_.push(std::move(msg));
}

void MatchingEngine::publishTickUpdate() {
    Price bidPrice = 0, askPrice = 0;
    Quantity bidQty = 0, askQty = 0;
    orderbook_.GetBestBidAsk(bidPrice, bidQty, askPrice, askQty);
    
    MarketDataMessage msg;
    msg.type = MessageType::TICK_UPDATE;
    
    TickUpdate& tick = msg.tick;
    tick.header = MessageHeader(MessageType::TICK_UPDATE, sizeof(TickUpdate));
    
    std::strncpy(tick.symbol, symbol_.c_str(), SYMBOL_LENGTH - 1);
    tick.symbol[SYMBOL_LENGTH - 1] = '\0';
    
    tick.lastPrice = lastTradePrice_;
    tick.lastQty = lastTradeQty_;
    tick.bidPrice = bidPrice;
    tick.askPrice = askPrice;
    tick.bidQty = bidQty;
    tick.askQty = askQty;
    
    feedQueue_.push(std::move(msg));
}

void MatchingEngine::getBestBidAsk(Price& bidPrice, Quantity& bidQty, 
                                    Price& askPrice, Quantity& askQty) {
    orderbook_.GetBestBidAsk(bidPrice, bidQty, askPrice, askQty);
}
