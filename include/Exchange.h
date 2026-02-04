/**
 * @file Exchange.h
 * @brief Main exchange coordinator that orchestrates all components.
 * 
 * The Exchange class is the top-level component that manages the lifecycle
 * of all exchange subsystems including the matching engine, network server,
 * session management, and market data distribution.
 */

#pragma once

#ifndef EXCHANGE_H
#define EXCHANGE_H

#include <memory>
#include <string>
#include <atomic>
#include <csignal>

#include "Protocol.h"
#include "LockFreeQueue.h"
#include "SessionManager.h"
#include "FixTcpServer.h"
#include "MatchingEngine.h"
#include "FeedHandler.h"
#include "DummyGenerator.h"

/**
 * @struct ExchangeConfiguration
 * @brief Configuration parameters for the exchange.
 * 
 * Contains all configurable settings for the exchange including network
 * ports, multicast settings, symbol configuration, and dummy generator options.
 */
struct ExchangeConfiguration {
    uint16_t fixPort{ExchangeConfig::TCP_PORT};  ///< FIX TCP port for client connections
    std::string multicastGroup{ExchangeConfig::MULTICAST_GROUP};  ///< UDP multicast group address
    uint16_t multicastPort{ExchangeConfig::MULTICAST_PORT};  ///< UDP multicast port
    std::string symbol{ExchangeConfig::DEFAULT_SYMBOL};  ///< Trading symbol
    bool publishFixMarketData{true};  ///< Also publish FIX W/X over UDP multicast
    std::string mdSenderCompId{"EXCHANGE"};  ///< FIX SenderCompID for market data
    std::string mdTargetCompId{"MD"};  ///< FIX TargetCompID for market data
    bool enableDummyGenerator{false};  ///< Enable dummy order generator for testing
    DummyGeneratorConfig dummyConfig;  ///< Dummy generator configuration
};

/**
 * @class Exchange
 * @brief Main exchange coordinator that manages all subsystems.
 * 
 * The Exchange class orchestrates the following components:
 * - FixTcpServer: Handles client connections via FIX over TCP
 * - SessionManager: Tracks client sessions and routes messages
 * - MatchingEngine: Processes orders and executes trades
 * - FeedHandler: Publishes market data via UDP multicast
 * - DummyGenerator: (Optional) Generates test orders
 * 
 * @par Threading Model
 * - Network Thread: FIX TCP server event loop
 * - Matching Thread: Single-threaded order matching
 * - Feed Thread: Market data publishing
 * - Dummy Generator Thread: (Optional) Test order generation
 * 
 * @par Example Usage
 * @code
 * ExchangeConfiguration config;
 * config.fixPort = 12345;
 * config.enableDummyGenerator = true;
 * 
 * Exchange exchange(config);
 * if (exchange.start()) {
 *     exchange.waitForShutdown();
 * }
 * @endcode
 */
class Exchange {
public:
    /**
     * @brief Construct a new Exchange with the given configuration.
     * @param config Exchange configuration parameters.
     */
    explicit Exchange(const ExchangeConfiguration& config);
    
    /**
     * @brief Destructor. Stops all components if running.
     */
    ~Exchange();
    
    /**
     * @brief Start all exchange components.
     * 
     * Starts components in the following order:
     * 1. Feed Handler (market data publisher)
     * 2. Matching Engine
     * 3. ZMQ Server
     * 4. Dummy Generator (if enabled)
     * 
     * @return true if all components started successfully, false otherwise.
     */
    bool start();
    
    /**
     * @brief Stop all exchange components gracefully.
     * 
     * Stops components in reverse order of startup to ensure
     * proper cleanup and message draining.
     */
    void stop();
    
    /**
     * @brief Check if the exchange is currently running.
     * @return true if running, false otherwise.
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Block until a shutdown signal (SIGINT/SIGTERM) is received.
     * 
     * This method sets up signal handlers and blocks until Ctrl+C
     * or a termination signal is received, then initiates shutdown.
     */
    void waitForShutdown();
    
    /**
     * @brief Print exchange statistics to stdout.
     * 
     * Outputs statistics including orders processed, trades executed,
     * active sessions, and published market data counts.
     */
    void printStatistics() const;
    
    /**
     * @brief Get reference to the matching engine.
     * @return Reference to MatchingEngine instance.
     */
    MatchingEngine& getMatchingEngine() { return *matchingEngine_; }
    
    /**
     * @brief Get reference to the session manager.
     * @return Reference to SessionManager instance.
     */
    SessionManager& getSessionManager() { return *sessionManager_; }
    
private:
    void setupSignalHandlers();
    void onSessionDisconnect(SessionId sessionId, const std::unordered_set<OrderId>& orderIds);
    
private:
    ExchangeConfiguration config_;
    
    using InboundQueue = MPSCQueue<InboundMessage, INBOUND_QUEUE_SIZE>;
    using OutboundQueue = SPSCQueue<OutboundMessage, OUTBOUND_QUEUE_SIZE>;
    using FeedQueue = SPSCQueue<MarketDataMessage, FEED_QUEUE_SIZE>;
    
    std::unique_ptr<InboundQueue> inboundQueue_;
    std::unique_ptr<OutboundQueue> outboundQueue_;
    std::unique_ptr<FeedQueue> feedQueue_;
    
    std::unique_ptr<SessionManager> sessionManager_;
    std::unique_ptr<FixTcpServer> fixServer_;
    std::unique_ptr<MatchingEngine> matchingEngine_;
    std::unique_ptr<FeedHandler> feedHandler_;
    std::unique_ptr<DummyGenerator> dummyGenerator_;
    
    std::atomic<bool> running_{false};
    
    static std::atomic<bool> shutdownRequested_;
    static void signalHandler(int signal);
};

#endif // EXCHANGE_H
