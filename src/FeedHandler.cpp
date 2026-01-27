#include "FeedHandler.h"

#include <iostream>
#include <cstring>

FeedHandler::FeedHandler(FeedQueue& feedQueue,
                         const std::string& multicastGroup,
                         uint16_t multicastPort,
                         const std::string& symbol)
    : feedQueue_(feedQueue)
    , multicast_(multicastGroup, multicastPort)
    , symbol_(symbol)
{
}

FeedHandler::~FeedHandler() {
    stop();
}

bool FeedHandler::start() {
    if (running_.exchange(true)) {
        return false;  // Already running
    }
    
    if (!multicast_.initialize()) {
        running_.store(false);
        return false;
    }
    
    feedThread_ = std::thread(&FeedHandler::feedLoop, this);
    
    std::cout << "Feed Handler started for symbol: " << symbol_ << std::endl;
    return true;
}

void FeedHandler::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    if (feedThread_.joinable()) {
        feedThread_.join();
    }
    
    multicast_.shutdown();
    
    std::cout << "Feed Handler stopped (ticks: " << ticksPublished_.load() 
              << ", trades: " << tradesPublished_.load() << ")" << std::endl;
}

void FeedHandler::feedLoop() {
    while (running_.load()) {
        // Process messages from the feed queue
        while (auto msgOpt = feedQueue_.pop()) {
            processMessage(*msgOpt);
        }
        
        // Small sleep to avoid busy-waiting when queue is empty
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Drain remaining messages before stopping
    while (auto msgOpt = feedQueue_.pop()) {
        processMessage(*msgOpt);
    }
}

void FeedHandler::processMessage(const MarketDataMessage& msg) {
    switch (msg.type) {
        case MessageType::TICK_UPDATE:
            if (multicast_.publishTick(msg.tick)) {
                ticksPublished_.fetch_add(1, std::memory_order_relaxed);
            }
            break;
            
        case MessageType::TRADE_UPDATE:
            if (multicast_.publishTrade(msg.trade)) {
                tradesPublished_.fetch_add(1, std::memory_order_relaxed);
            }
            break;
            
        case MessageType::ORDERBOOK_SNAPSHOT:
            multicast_.publishSnapshot(msg.snapshot);
            break;
            
        default:
            break;
    }
}

void FeedHandler::publishTick(Price lastPrice, Quantity lastQty,
                               Price bidPrice, Price askPrice,
                               Quantity bidQty, Quantity askQty) {
    MarketDataMessage msg;
    msg.type = MessageType::TICK_UPDATE;
    
    TickUpdate& tick = msg.tick;
    tick.header = MessageHeader(MessageType::TICK_UPDATE, sizeof(TickUpdate));
    
    // Copy symbol (truncate if necessary)
    std::strncpy(tick.symbol, symbol_.c_str(), SYMBOL_LENGTH - 1);
    tick.symbol[SYMBOL_LENGTH - 1] = '\0';
    
    tick.lastPrice = lastPrice;
    tick.lastQty = lastQty;
    tick.bidPrice = bidPrice;
    tick.askPrice = askPrice;
    tick.bidQty = bidQty;
    tick.askQty = askQty;
    
    // Try to enqueue for feed thread to publish
    if (!feedQueue_.push(std::move(msg))) {
        // Queue full - could log or handle differently
    }
}

void FeedHandler::publishTrade(TradeId tradeId, Price price, Quantity quantity, BuySell aggressorSide) {
    MarketDataMessage msg;
    msg.type = MessageType::TRADE_UPDATE;
    
    TradeUpdate& trade = msg.trade;
    trade.header = MessageHeader(MessageType::TRADE_UPDATE, sizeof(TradeUpdate));
    
    std::strncpy(trade.symbol, symbol_.c_str(), SYMBOL_LENGTH - 1);
    trade.symbol[SYMBOL_LENGTH - 1] = '\0';
    
    trade.tradeId = tradeId;
    trade.price = price;
    trade.quantity = quantity;
    trade.aggressorSide = static_cast<uint8_t>(aggressorSide);
    
    if (!feedQueue_.push(std::move(msg))) {
        // Queue full
    }
}
