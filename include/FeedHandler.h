/**
 * @file FeedHandler.h
 * @brief Market data feed handler with UDP multicast publishing.
 * 
 * Receives market data from the matching engine via queue and
 * publishes it to clients via UDP multicast.
 */

#pragma once

#ifndef FEEDHANDLER_H
#define FEEDHANDLER_H

#include <atomic>
#include <thread>
#include <string>
#include <cstring>

#include "Protocol.h"
#include "LockFreeQueue.h"
#include "UdpMulticast.h"

/**
 * @class FeedHandler
 * @brief Publishes market data via UDP multicast.
 * 
 * The FeedHandler runs in its own thread, processing market data
 * messages from the feed queue and publishing them via UDP multicast.
 * 
 * @par Supported Message Types
 * - TickUpdate: Best bid/ask and last trade info
 * - TradeUpdate: Individual trade details
 * - OrderbookSnapshot: Top-of-book depth
 * 
 * @par Threading
 * Runs in dedicated thread, consuming from SPSC feed queue.
 */
class FeedHandler {
public:
    using FeedQueue = SPSCQueue<MarketDataMessage, FEED_QUEUE_SIZE>;
    
    /**
     * @brief Construct a new FeedHandler.
     * @param feedQueue Queue for incoming market data messages.
     * @param multicastGroup UDP multicast group address.
     * @param multicastPort UDP multicast port.
     * @param symbol Trading symbol for this feed.
     */
    FeedHandler(FeedQueue& feedQueue,
                const std::string& multicastGroup,
                uint16_t multicastPort,
                const std::string& symbol,
                bool publishFixMarketData,
                const std::string& mdSenderCompId,
                const std::string& mdTargetCompId);
    
    /**
     * @brief Destructor. Stops feed thread if running.
     */
    ~FeedHandler();
    
    /**
     * @brief Start the feed handler thread.
     * @return true if started successfully, false on error.
     */
    bool start();
    
    /**
     * @brief Stop the feed handler gracefully.
     */
    void stop();
    
    /**
     * @brief Check if the feed handler is running.
     * @return true if running, false otherwise.
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Publish a tick update message.
     * @param lastPrice Last traded price.
     * @param lastQty Last traded quantity.
     * @param bidPrice Best bid price.
     * @param askPrice Best ask price.
     * @param bidQty Best bid quantity.
     * @param askQty Best ask quantity.
     */
    void publishTick(Price lastPrice, Quantity lastQty,
                     Price bidPrice, Price askPrice,
                     Quantity bidQty, Quantity askQty);
    
    /**
     * @brief Publish a trade update message.
     * @param tradeId Unique trade identifier.
     * @param price Trade execution price.
     * @param quantity Trade quantity.
     * @param aggressorSide Side that initiated the trade.
     */
    void publishTrade(TradeId tradeId, Price price, Quantity quantity, BuySell aggressorSide);
    
    /**
     * @brief Get count of tick updates published.
     * @return Number of tick messages published.
     */
    uint64_t getTicksPublished() const { return ticksPublished_.load(); }
    
    /**
     * @brief Get count of trade updates published.
     * @return Number of trade messages published.
     */
    uint64_t getTradesPublished() const { return tradesPublished_.load(); }
    
private:
    void feedLoop();
    void processMessage(const MarketDataMessage& msg);
    void publishFixIncremental(const MarketDataMessage& msg);
    void publishFixSnapshot(const MarketDataMessage& msg);
    std::string currentUtcTimestamp() const;
    
private:
    FeedQueue& feedQueue_;
    UdpMulticast multicast_;
    std::string symbol_;
    bool publishFixMarketData_{false};
    std::string mdSenderCompId_;
    std::string mdTargetCompId_;
    std::atomic<uint64_t> fixSeq_{1};
    
    std::atomic<bool> running_{false};
    std::thread feedThread_;
    
    std::atomic<uint64_t> ticksPublished_{0};
    std::atomic<uint64_t> tradesPublished_{0};
};

#endif // FEEDHANDLER_H
