/**
 * @file DummyGenerator.h
 * @brief Dummy order generator for testing and simulation.
 * 
 * Generates synthetic orders to feed into the matching engine
 * for testing purposes without real client connections.
 */

#pragma once

#ifndef DUMMYGENERATOR_H
#define DUMMYGENERATOR_H

#include <atomic>
#include <thread>
#include <random>
#include <chrono>

#include "Protocol.h"
#include "LockFreeQueue.h"

/**
 * @struct DummyGeneratorConfig
 * @brief Configuration for the dummy order generator.
 */
struct DummyGeneratorConfig {
    Price basePrice{1000};        ///< Center price for order generation
    Price priceRange{100};        ///< +/- range from base price
    Price tickSize{1};            ///< Minimum price increment
    
    Quantity minQuantity{1};      ///< Minimum order quantity
    Quantity maxQuantity{100};    ///< Maximum order quantity
    
    uint32_t ordersPerSecond{10}; ///< Rate of order generation
    double buyRatio{0.5};         ///< Probability of buy order (0.0 - 1.0)
    
    double limitRatio{0.8};       ///< Fraction of LIMIT orders
    double marketRatio{0.1};      ///< Fraction of MARKET orders
    double iocRatio{0.1};         ///< Fraction of IOC orders
};

/**
 * @class DummyGenerator
 * @brief Generates synthetic orders for testing.
 * 
 * The DummyGenerator creates realistic order flow by:
 * - Simulating price movements via random walk
 * - Generating orders at configurable rate
 * - Mixing order types (LIMIT, MARKET, IOC)
 * - Balancing buy/sell orders
 * 
 * Orders are pushed to the inbound queue with special session ID
 * (DUMMY_SESSION_ID) to distinguish them from real client orders.
 * 
 * @par Example Usage
 * @code
 * DummyGeneratorConfig config;
 * config.ordersPerSecond = 20;
 * config.buyRatio = 0.6;
 * 
 * DummyGenerator generator(inboundQueue);
 * generator.setConfig(config);
 * generator.start();
 * @endcode
 */
class DummyGenerator {
public:
    using InboundQueue = MPSCQueue<InboundMessage, INBOUND_QUEUE_SIZE>;
    
    /**
     * @brief Construct a new DummyGenerator.
     * @param inboundQueue Queue to push generated orders.
     */
    explicit DummyGenerator(InboundQueue& inboundQueue);
    
    /**
     * @brief Destructor. Stops generator if running.
     */
    ~DummyGenerator();
    
    /**
     * @brief Set generator configuration.
     * @param config Configuration parameters.
     */
    void setConfig(const DummyGeneratorConfig& config);
    
    /**
     * @brief Get current configuration.
     * @return Reference to current configuration.
     */
    const DummyGeneratorConfig& getConfig() const { return config_; }
    
    /**
     * @brief Start order generation.
     * @return true if started successfully, false if already running.
     */
    bool start();
    
    /**
     * @brief Stop order generation.
     */
    void stop();
    
    /**
     * @brief Check if generator is running.
     * @return true if running, false otherwise.
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Get count of orders generated.
     * @return Total orders generated since start.
     */
    uint64_t getOrdersGenerated() const { return ordersGenerated_.load(); }
    
private:
    void generatorLoop();
    void generateOrder();
    
    Price generatePrice();
    Quantity generateQuantity();
    BuySell generateSide();
    OrderType generateOrderType();
    
private:
    InboundQueue& inboundQueue_;
    DummyGeneratorConfig config_;
    
    std::atomic<bool> running_{false};
    std::thread generatorThread_;
    
    std::atomic<uint64_t> ordersGenerated_{0};
    std::atomic<ClientOrderId> nextClientOrderId_{1};
    
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniformDist_{0.0, 1.0};
    
    Price currentPrice_;
};

#endif // DUMMYGENERATOR_H
