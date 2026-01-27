#include "Orderbook.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace
{
bool isAggressiveOrder(const OrderPointer& order)
{
    if (!order) {
        return false;
    }
    return order->getOrderType() == OrderType::MARKET || order->getOrderType() == OrderType::IOC;
}
}

Orderbook::Orderbook(/* args */) = default;

Orderbook::~Orderbook() = default;

void Orderbook::AddOrder(const OrderPointer& order)
{
    addToBook(order);
}

void Orderbook::ModifyOrder(OrderId orderId, Price newPrice, Quantity newQuantity)
{
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        return;
    }

    OrderPointer order = it->second;
    if (!order) {
        orders_.erase(it);
        return;
    }

    if (order->getPrice() == newPrice && order->getQuantity() == newQuantity) {
        return;
    }

    Quantity remainingQuantity = order->getQuantity();
    if (remainingQuantity > 0) {
        updateLevelOnFill(order->getPrice(), remainingQuantity, true);
    }

    removeOrderFromBook(order);

    order->setPrice(newPrice);
    order->setQuantity(newQuantity);
    order->setOrderStatus(OrderStatus::MODIFY);

    addToBook(order);
}

void Orderbook::CancelOrder(OrderId orderId)
{
    auto it = orders_.find(orderId);
    if (it == orders_.end()) {
        return;
    }

    OrderPointer order = it->second;
    if (!order) {
        orders_.erase(it);
        return;
    }

    Quantity remainingQuantity = order->getQuantity();
    if (remainingQuantity > 0) {
        updateLevelOnFill(order->getPrice(), remainingQuantity, true);
    }

    removeOrderFromBook(order);
    order->setQuantity(0);
    order->setOrderStatus(OrderStatus::CANCELLED);
    clearOrderRecord(orderId);
}

void Orderbook::PrintOrderbook() const
{
    std::cout << "================ ORDERBOOK ================\n";
    std::cout << "   Price       BidQty | AskQty" << '\n';
    std::cout << "-------------------------------------------" << '\n';

    auto bidIt = bids_.begin();
    auto askIt = asks_.begin();
    while (bidIt != bids_.end() || askIt != asks_.end()) {
        Price price = 0;
        Quantity bidQty = 0;
        Quantity askQty = 0;

        if (bidIt != bids_.end()) {
            price = bidIt->first;
            for (const auto& ord : bidIt->second) {
                bidQty += ord->getQuantity();
            }
        }

        if (askIt != asks_.end() && (bidIt == bids_.end() || askIt->first <= price)) {
            price = askIt->first;
            askQty = 0;
            for (const auto& ord : askIt->second) {
                askQty += ord->getQuantity();
            }
        }

        if (bidIt != bids_.end() && bidIt->first == price) {
            ++bidIt;
        }
        if (askIt != asks_.end() && askIt->first == price) {
            ++askIt;
        }

        std::cout << std::setw(8) << price << "   " << std::setw(8) << bidQty << " | " << std::setw(8) << askQty << '\n';
    }

    std::cout << "===========================================\n";
}

bool Orderbook::priceCrossed(Price incomingPrice, Price restingPrice, BuySell side, OrderType type) const
{
    if (type == OrderType::MARKET || type == OrderType::IOC) {
        return true;
    }

    if (side == BuySell::BUY) {
        return incomingPrice >= restingPrice;
    }
    if (side == BuySell::SELL) {
        return incomingPrice <= restingPrice;
    }
    return false;
}

void Orderbook::addToBook(const OrderPointer& order)
{
    trades.clear();
    if (!order) {
        return;
    }

    const Quantity originalQuantity = order->getQuantity();
    Quantity remaining = originalQuantity;
    const BuySell side = order->getBuySell();

    if (side == BuySell::BUY || side == BuySell::SELL) {
        auto matchAgainst = [&](auto& contraBook) {
            while (remaining > 0 && !contraBook.empty()) {
                auto levelIt = contraBook.begin();
                Price restingPrice = levelIt->first;

                if (!priceCrossed(order->getPrice(), restingPrice, side, order->getOrderType())) {
                    break;
                }

                auto& levelOrders = levelIt->second;
                auto orderIt = levelOrders.begin();

                while (orderIt != levelOrders.end() && remaining > 0) {
                    OrderPointer restingOrder = *orderIt;
                    Quantity executed = std::min(remaining, restingOrder->getQuantity());

                    remaining -= executed;
                    restingOrder->reduceQuantity(executed);
                    bool restingClosed = restingOrder->isFilled();

                    updateLevelOnFill(restingPrice, executed, restingClosed);

                    Trade trade{};
                    trade.price = restingPrice;
                    trade.quantity = executed;
                    trade.tradeType = (restingClosed && remaining == 0) ? TradeType::FULL : TradeType::PARTIAL;
                    trade.buyOrderId = (side == BuySell::BUY) ? order->getOrderId() : restingOrder->getOrderId();
                    trade.sellOrderId = (side == BuySell::SELL) ? order->getOrderId() : restingOrder->getOrderId();
                    trades.push_back(trade);

                    if (restingClosed) {
                        restingOrder->setOrderStatus(OrderStatus::TRADED);
                        orderIt = levelOrders.erase(orderIt);
                        clearOrderRecord(restingOrder->getOrderId());
                    } else {
                        restingOrder->setOrderStatus(OrderStatus::MODIFY);
                        ++orderIt;
                    }
                }

                if (levelOrders.empty()) {
                    contraBook.erase(levelIt);
                }
            }
        };

        if (side == BuySell::BUY) {
            matchAgainst(asks_);
        } else {
            matchAgainst(bids_);
        }
    }

    order->setQuantity(remaining);

    if (remaining == 0 && originalQuantity > 0) {
        order->setOrderStatus(OrderStatus::TRADED);
        clearOrderRecord(order->getOrderId());
        return;
    }

    if (isAggressiveOrder(order)) {
        order->setOrderStatus(OrderStatus::CANCELLED);
        order->setQuantity(0);
        return;
    }

    if (remaining < originalQuantity) {
        order->setOrderStatus(OrderStatus::MODIFY);
    } else {
        if (order->getOrderStatus() != OrderStatus::MODIFY) {
            order->setOrderStatus(OrderStatus::NEW);
        }
    }

    if (order->getQuantity() > 0) {
        if (order->getBuySell() == BuySell::BUY) {
            auto [levelIt, inserted] = bids_.try_emplace(order->getPrice());
            levelIt->second.push_back(order);
        } else {
            auto [levelIt, inserted] = asks_.try_emplace(order->getPrice());
            levelIt->second.push_back(order);
        }

        updateLevelOnAdd(order->getPrice(), order->getQuantity());
        orders_[order->getOrderId()] = order;
    }
}

void Orderbook::updateLevelOnAdd(Price price, Quantity quantity)
{
    if (quantity == 0) {
        return;
    }

    auto& data = price_to_leveldata_[price];
    data.price = price;
    data.totalQuantity += quantity;
    data.numberOfOrders += 1;
}

void Orderbook::updateLevelOnFill(Price price, Quantity quantity, bool orderClosed)
{
    if (quantity == 0) {
        return;
    }

    auto it = price_to_leveldata_.find(price);
    if (it == price_to_leveldata_.end()) {
        return;
    }

    LevelData& data = it->second;
    if (data.totalQuantity <= quantity) {
        data.totalQuantity = 0;
    } else {
        data.totalQuantity -= quantity;
    }

    if (orderClosed && data.numberOfOrders > 0) {
        --data.numberOfOrders;
    }

    if (data.totalQuantity == 0 || data.numberOfOrders == 0) {
        price_to_leveldata_.erase(it);
    }
}

void Orderbook::removeOrderFromBook(const OrderPointer& order)
{
    if (!order) {
        return;
    }

    auto eraseFromBook = [&](auto& book) {
        auto levelIt = book.find(order->getPrice());
        if (levelIt == book.end()) {
            return;
        }

        auto& levelOrders = levelIt->second;
        for (auto it = levelOrders.begin(); it != levelOrders.end(); ++it) {
            if ((*it)->getOrderId() == order->getOrderId()) {
                levelOrders.erase(it);
                break;
            }
        }

        if (levelOrders.empty()) {
            book.erase(levelIt);
        }
    };

    if (order->getBuySell() == BuySell::BUY) {
        eraseFromBook(bids_);
    } else {
        eraseFromBook(asks_);
    }
}

void Orderbook::clearOrderRecord(OrderId orderId)
{
    orders_.erase(orderId);
}

bool Orderbook::GetBestBid(Price& price, Quantity& qty) const
{
    if (bids_.empty()) {
        price = 0;
        qty = 0;
        return false;
    }
    const auto& level = *bids_.begin();
    price = level.first;
    qty = 0;
    for (const auto& ord : level.second) {
        qty += ord->getQuantity();
    }
    return qty > 0;
}

bool Orderbook::GetBestAsk(Price& price, Quantity& qty) const
{
    if (asks_.empty()) {
        price = 0;
        qty = 0;
        return false;
    }
    const auto& level = *asks_.begin();
    price = level.first;
    qty = 0;
    for (const auto& ord : level.second) {
        qty += ord->getQuantity();
    }
    return qty > 0;
}

bool Orderbook::GetBestBidAsk(Price& bidPrice, Quantity& bidQty,
                              Price& askPrice, Quantity& askQty) const
{
    bool hasBid = GetBestBid(bidPrice, bidQty);
    bool hasAsk = GetBestAsk(askPrice, askQty);
    if (!hasBid) {
        bidPrice = 0;
        bidQty = 0;
    }
    if (!hasAsk) {
        askPrice = 0;
        askQty = 0;
    }
    return hasBid || hasAsk;
}