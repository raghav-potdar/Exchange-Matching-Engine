#pragma once

#include <vector>
#include <cstdint>

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using Timestamp = std::uint64_t;
using OrderIds = std::vector<OrderId>;

// Exchange-specific types
using SessionId = std::uint32_t;
using ClientOrderId = std::uint64_t;
using TradeId = std::uint64_t;

constexpr SessionId INVALID_SESSION_ID = 0;
constexpr SessionId DUMMY_SESSION_ID = 0xFFFFFFFF;  // For dummy generator orders