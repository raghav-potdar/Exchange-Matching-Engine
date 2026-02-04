#include "FeedHandler.h"
#include "FixMessage.h"

#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

FeedHandler::FeedHandler(FeedQueue& feedQueue,
                         const std::string& multicastGroup,
                         uint16_t multicastPort,
                         const std::string& symbol,
                         bool publishFixMarketData,
                         const std::string& mdSenderCompId,
                         const std::string& mdTargetCompId)
    : feedQueue_(feedQueue)
    , multicast_(multicastGroup, multicastPort)
    , symbol_(symbol)
    , publishFixMarketData_(publishFixMarketData)
    , mdSenderCompId_(mdSenderCompId)
    , mdTargetCompId_(mdTargetCompId)
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
            if (publishFixMarketData_) {
                publishFixIncremental(msg);
            }
            break;
            
        case MessageType::TRADE_UPDATE:
            if (multicast_.publishTrade(msg.trade)) {
                tradesPublished_.fetch_add(1, std::memory_order_relaxed);
            }
            if (publishFixMarketData_) {
                publishFixIncremental(msg);
            }
            break;
            
        case MessageType::ORDERBOOK_SNAPSHOT:
            multicast_.publishSnapshot(msg.snapshot);
            if (publishFixMarketData_) {
                publishFixSnapshot(msg);
            }
            break;
            
        default:
            break;
    }
}

void FeedHandler::publishFixIncremental(const MarketDataMessage& msg) {
    FixMessage fix;
    fix.set(35, "X");
    fix.set(49, mdSenderCompId_);
    fix.set(56, mdTargetCompId_);
    fix.set(34, std::to_string(fixSeq_.fetch_add(1)));
    fix.set(52, currentUtcTimestamp());
    fix.set(55, symbol_);

    std::vector<FixField> entries;
    size_t entryCount = 0;
    if (msg.type == MessageType::TICK_UPDATE) {
        const TickUpdate& t = msg.tick;
        entries.push_back({279, "1"});  // Change
        entries.push_back({269, "0"});
        entries.push_back({270, std::to_string(t.bidPrice)});
        entries.push_back({271, std::to_string(t.bidQty)});
        entryCount++;

        entries.push_back({279, "1"});  // Change
        entries.push_back({269, "1"});
        entries.push_back({270, std::to_string(t.askPrice)});
        entries.push_back({271, std::to_string(t.askQty)});
        entryCount++;

        if (t.lastQty > 0) {
            entries.push_back({279, "0"});
            entries.push_back({269, "2"});
            entries.push_back({270, std::to_string(t.lastPrice)});
            entries.push_back({271, std::to_string(t.lastQty)});
            entryCount++;
        }
    } else if (msg.type == MessageType::TRADE_UPDATE) {
        const TradeUpdate& t = msg.trade;
        entries.push_back({279, "0"});
        entries.push_back({269, "2"});
        entries.push_back({270, std::to_string(t.price)});
        entries.push_back({271, std::to_string(t.quantity)});
        entryCount++;
    }

    if (entries.empty()) {
        return;
    }

    fix.set(268, std::to_string(entryCount));
    for (const auto& field : entries) {
        fix.set(field.tag, field.value);
    }

    std::string raw = fix.serialize("FIXT.1.1");
    multicast_.publish(raw.data(), raw.size());
}

void FeedHandler::publishFixSnapshot(const MarketDataMessage& msg) {
    const OrderbookSnapshot& s = msg.snapshot;
    FixMessage fix;
    fix.set(35, "W");
    fix.set(49, mdSenderCompId_);
    fix.set(56, mdTargetCompId_);
    fix.set(34, std::to_string(fixSeq_.fetch_add(1)));
    fix.set(52, currentUtcTimestamp());
    fix.set(55, symbol_);

    size_t totalEntries = static_cast<size_t>(s.bidLevels + s.askLevels);
    fix.set(268, std::to_string(totalEntries));

    for (size_t i = 0; i < s.bidLevels; ++i) {
        fix.set(269, "0");
        fix.set(270, std::to_string(s.bids[i].price));
        fix.set(271, std::to_string(s.bids[i].quantity));
    }
    for (size_t i = 0; i < s.askLevels; ++i) {
        fix.set(269, "1");
        fix.set(270, std::to_string(s.asks[i].price));
        fix.set(271, std::to_string(s.asks[i].quantity));
    }

    std::string raw = fix.serialize("FIXT.1.1");
    multicast_.publish(raw.data(), raw.size());
}

std::string FeedHandler::currentUtcTimestamp() const {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = time_point_cast<seconds>(now);
    auto ms = duration_cast<milliseconds>(now - secs).count();

    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H:%M:%S");
    oss << "." << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
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
