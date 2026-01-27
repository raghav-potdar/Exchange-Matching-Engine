#include "DummyGenerator.h"

#include <iostream>
#include <cstring>

DummyGenerator::DummyGenerator(InboundQueue& inboundQueue)
    : inboundQueue_(inboundQueue)
    , rng_(std::random_device{}())
    , currentPrice_(config_.basePrice)
{
}

DummyGenerator::~DummyGenerator() {
    stop();
}

void DummyGenerator::setConfig(const DummyGeneratorConfig& config) {
    config_ = config;
    currentPrice_ = config_.basePrice;
}

bool DummyGenerator::start() {
    if (running_.exchange(true)) {
        return false;  // Already running
    }
    
    currentPrice_ = config_.basePrice;
    generatorThread_ = std::thread(&DummyGenerator::generatorLoop, this);
    
    std::cout << "Dummy Generator started (rate: " << config_.ordersPerSecond << " orders/sec)" << std::endl;
    return true;
}

void DummyGenerator::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    if (generatorThread_.joinable()) {
        generatorThread_.join();
    }
    
    std::cout << "Dummy Generator stopped (generated " << ordersGenerated_.load() << " orders)" << std::endl;
}

void DummyGenerator::generatorLoop() {
    using namespace std::chrono;
    
    if (config_.ordersPerSecond == 0) {
        return;
    }
    
    // Calculate interval between orders
    auto intervalUs = microseconds(1000000 / config_.ordersPerSecond);
    auto nextOrderTime = steady_clock::now();
    
    while (running_.load()) {
        auto now = steady_clock::now();
        
        if (now >= nextOrderTime) {
            generateOrder();
            nextOrderTime += intervalUs;
            
            // If we've fallen behind, catch up
            if (nextOrderTime < now) {
                nextOrderTime = now + intervalUs;
            }
        } else {
            // Sleep until next order time
            std::this_thread::sleep_until(nextOrderTime);
        }
    }
}

void DummyGenerator::generateOrder() {
    InboundMessage msg;
    msg.sessionId = DUMMY_SESSION_ID;
    msg.type = MessageType::NEW_ORDER_REQUEST;
    
    NewOrderRequest& request = msg.newOrder;
    request.header = MessageHeader(MessageType::NEW_ORDER_REQUEST, sizeof(NewOrderRequest));
    request.clientOrderId = nextClientOrderId_.fetch_add(1);
    
    BuySell side = generateSide();
    OrderType orderType = generateOrderType();
    
    request.side = static_cast<uint8_t>(side);
    request.orderType = static_cast<uint8_t>(orderType);
    request.quantity = generateQuantity();
    
    // For MARKET orders, price is 0 (will match at market)
    if (orderType == OrderType::MARKET) {
        request.price = 0;
    } else {
        // Generate a price around the current market
        Price price = generatePrice();
        
        // Adjust price based on side to make trades more likely
        if (side == BuySell::BUY) {
            // Buyers might bid slightly lower
            price -= static_cast<Price>(uniformDist_(rng_) * 5) * config_.tickSize;
        } else {
            // Sellers might ask slightly higher
            price += static_cast<Price>(uniformDist_(rng_) * 5) * config_.tickSize;
        }
        
        request.price = price;
    }
    
    // Try to enqueue the order
    if (inboundQueue_.push(std::move(msg))) {
        ordersGenerated_.fetch_add(1, std::memory_order_relaxed);
    } else {
        // Queue full - order dropped
        std::cerr << "Dummy Generator: Inbound queue full, order dropped" << std::endl;
    }
}

Price DummyGenerator::generatePrice() {
    // Random walk for current price
    double priceChange = (uniformDist_(rng_) - 0.5) * 2.0 * config_.tickSize * 3;
    currentPrice_ += static_cast<Price>(priceChange);
    
    // Clamp to range
    Price minPrice = config_.basePrice - config_.priceRange;
    Price maxPrice = config_.basePrice + config_.priceRange;
    
    if (currentPrice_ < minPrice) currentPrice_ = minPrice;
    if (currentPrice_ > maxPrice) currentPrice_ = maxPrice;
    
    // Round to tick size
    Price price = (currentPrice_ / config_.tickSize) * config_.tickSize;
    if (price < 1) price = config_.tickSize;
    
    return price;
}

Quantity DummyGenerator::generateQuantity() {
    std::uniform_int_distribution<Quantity> dist(config_.minQuantity, config_.maxQuantity);
    return dist(rng_);
}

BuySell DummyGenerator::generateSide() {
    return uniformDist_(rng_) < config_.buyRatio ? BuySell::BUY : BuySell::SELL;
}

OrderType DummyGenerator::generateOrderType() {
    double r = uniformDist_(rng_);
    
    if (r < config_.limitRatio) {
        return OrderType::LIMIT;
    } else if (r < config_.limitRatio + config_.marketRatio) {
        return OrderType::MARKET;
    } else {
        return OrderType::IOC;
    }
}
