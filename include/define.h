#pragma once

#ifndef DEFINE_H
#define DEFINE_H

enum class BuySell {
    DEFAULT = 0,
    BUY = 1,
    SELL = 2
};

enum class OrderType {
    DEFAULT = 0,
    LIMIT = 1,
    IOC = 2,
    MARKET = 3,
    STOPLOSS = 4
};

enum class OrderStatus {
    DEFAULT = 0,
    NEW = 1,
    MODIFY = 2,
    TRADED = 3,
    CANCELLED = 4
};

enum class TradeType {
    DEFAULT = 0,
    PARTIAL = 1,
    FULL = 2
};

#endif // DEFINE_H