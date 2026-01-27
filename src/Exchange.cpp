#include "Exchange.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

// Static member initialization
std::atomic<bool> Exchange::shutdownRequested_{false};

Exchange::Exchange(const ExchangeConfiguration& config)
    : config_(config)
{
    // Create queues
    inboundQueue_ = std::make_unique<InboundQueue>();
    outboundQueue_ = std::make_unique<OutboundQueue>();
    feedQueue_ = std::make_unique<FeedQueue>();
    
    // Create session manager
    sessionManager_ = std::make_unique<SessionManager>(*inboundQueue_, *outboundQueue_);
    
    // Create ZMQ server
    zmqServer_ = std::make_unique<ZmqServer>(config_.zmqPort, *sessionManager_);
    
    // Create matching engine
    matchingEngine_ = std::make_unique<MatchingEngine>(
        *inboundQueue_, *outboundQueue_, *feedQueue_, config_.symbol);
    
    // Create feed handler
    feedHandler_ = std::make_unique<FeedHandler>(
        *feedQueue_, config_.multicastGroup, config_.multicastPort, config_.symbol);
    
    // Create dummy generator if enabled
    if (config_.enableDummyGenerator) {
        dummyGenerator_ = std::make_unique<DummyGenerator>(*inboundQueue_);
        dummyGenerator_->setConfig(config_.dummyConfig);
    }
    
    // Set up disconnect callback
    sessionManager_->setDisconnectCallback(
        [this](SessionId sessionId, const std::unordered_set<OrderId>& orderIds) {
            onSessionDisconnect(sessionId, orderIds);
        });
}

Exchange::~Exchange() {
    stop();
}

bool Exchange::start() {
    if (running_.exchange(true)) {
        return false;  // Already running
    }
    
    setupSignalHandlers();
    
    std::cout << "========================================" << std::endl;
    std::cout << "      Starting Exchange" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Symbol: " << config_.symbol << std::endl;
    std::cout << "ZMQ Port: " << config_.zmqPort << " (ROUTER/DEALER)" << std::endl;
    std::cout << "Multicast: " << config_.multicastGroup << ":" << config_.multicastPort << std::endl;
    std::cout << "Dummy Generator: " << (config_.enableDummyGenerator ? "Enabled" : "Disabled") << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Start components in order
    
    // 1. Start feed handler (market data publisher)
    if (!feedHandler_->start()) {
        std::cerr << "Failed to start Feed Handler" << std::endl;
        running_.store(false);
        return false;
    }
    
    // 2. Start matching engine
    if (!matchingEngine_->start()) {
        std::cerr << "Failed to start Matching Engine" << std::endl;
        feedHandler_->stop();
        running_.store(false);
        return false;
    }
    
    // 3. Start ZMQ server
    if (!zmqServer_->start()) {
        std::cerr << "Failed to start ZMQ Server" << std::endl;
        matchingEngine_->stop();
        feedHandler_->stop();
        running_.store(false);
        return false;
    }
    
    // 4. Start dummy generator if enabled
    if (dummyGenerator_) {
        if (!dummyGenerator_->start()) {
            std::cerr << "Failed to start Dummy Generator" << std::endl;
            // Non-fatal, continue without dummy generator
        }
    }
    
    std::cout << "Exchange started successfully!" << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    std::cout << "========================================" << std::endl;
    
    return true;
}

void Exchange::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "      Stopping Exchange" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Stop components in reverse order
    
    // 1. Stop dummy generator
    if (dummyGenerator_) {
        dummyGenerator_->stop();
    }
    
    // 2. Stop ZMQ server (stops accepting new connections)
    zmqServer_->stop();
    
    // 3. Stop matching engine (finish processing queued orders)
    matchingEngine_->stop();
    
    // 4. Stop feed handler
    feedHandler_->stop();
    
    // Print final statistics
    printStatistics();
    
    std::cout << "Exchange stopped." << std::endl;
    std::cout << "========================================" << std::endl;
}

void Exchange::waitForShutdown() {
    while (running_.load() && !shutdownRequested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (shutdownRequested_.load()) {
        stop();
    }
}

void Exchange::printStatistics() const {
    std::cout << "\n--- Exchange Statistics ---" << std::endl;
    std::cout << "Orders Processed: " << matchingEngine_->getOrdersProcessed() << std::endl;
    std::cout << "Trades Executed: " << matchingEngine_->getTradesExecuted() << std::endl;
    std::cout << "Active Sessions: " << sessionManager_->getActiveSessionCount() << std::endl;
    std::cout << "Ticks Published: " << feedHandler_->getTicksPublished() << std::endl;
    std::cout << "Trades Published: " << feedHandler_->getTradesPublished() << std::endl;
    
    if (dummyGenerator_) {
        std::cout << "Dummy Orders Generated: " << dummyGenerator_->getOrdersGenerated() << std::endl;
    }
    std::cout << "----------------------------" << std::endl;
}

void Exchange::setupSignalHandlers() {
    shutdownRequested_.store(false);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

void Exchange::signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        shutdownRequested_.store(true);
    }
}

void Exchange::onSessionDisconnect(SessionId sessionId, const std::unordered_set<OrderId>& orderIds) {
    // Cancel all orders belonging to the disconnected session
    if (!orderIds.empty()) {
        std::cout << "Cancelling " << orderIds.size() << " orders for disconnected session " 
                  << sessionId << std::endl;
        matchingEngine_->cancelSessionOrders(sessionId, orderIds);
    }
}
