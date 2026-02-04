# Exchange Matching Engine

A high-performance exchange matching engine with orderbook implementation in C++20. Features FIX-over-TCP client connectivity, UDP multicast market data distribution, and a price-time priority matching algorithm.

## Architecture

```
                    ┌─────────────────────────────────────────────────────────────┐
                    │                      Exchange Engine                         │
                    │                                                              │
┌──────────┐        │  ┌─────────────┐    ┌──────────────┐    ┌───────────────┐  │
│  Dummy   │────────┼─▶│   Inbound   │───▶│   Matching   │◀──▶│   Orderbook   │  │
│Generator │        │  │    Queue    │    │    Engine    │    │               │  │
└──────────┘        │  └─────────────┘    └──────────────┘    └───────────────┘  │
                    │         ▲                  │                               │
                    │         │                  ▼                               │
┌──────────┐        │  ┌─────────────┐    ┌──────────────┐    ┌───────────────┐  │
│  Client  │◀──────▶│  │   Session   │◀──▶│   Outbound   │    │     Feed      │  │
│ (FIXTCP) │  TCP   │  │   Manager   │    │    Queue     │    │    Handler    │  │
└──────────┘ ROUTER │  └─────────────┘    └──────────────┘    └───────────────┘  │
                    │                                                │            │
                    │                                                ▼            │
                    │                                         ┌───────────────┐  │
┌──────────┐        │                                         │      UDP      │  │
│  Market  │◀───────┼─────────────────────────────────────────│   Multicast   │  │
│   Data   │  UDP   │                                         │   Publisher   │  │
│  Client  │        │                                         └───────────────┘  │
└──────────┘        └─────────────────────────────────────────────────────────────┘
```

## Features

- **Price-Time Priority Matching**: Standard exchange matching algorithm
- **Order Types**: LIMIT, MARKET, IOC (Immediate-Or-Cancel)
- **FIX over TCP**: FIX session + order flow for client order entry
- **UDP Multicast**: Low-latency market data distribution
- **FIX Market Data**: Optional FIX W/X messages over multicast
- **Lock-Free Queues**: High-performance inter-thread communication
- **Session Management**: Automatic order cancellation on disconnect
- **Dummy Generator**: Built-in order generator for testing

## Building

### Prerequisites

- CMake 3.14+
- C++20 compatible compiler (GCC 10+, Clang 12+)
- No external dependencies (libzmq built from source via CMake FetchContent)

### Build Instructions

```bash
# Clone the repository
git clone <repository-url>
cd Exchange-Matching-Engine

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Run the exchange
./exchange --dummy
```

### Build Options

```bash
# Enable address/undefined behavior sanitizers
cmake -DENABLE_SANITIZERS=ON ..
```

## Usage

```bash
./exchange [options]

Options:
  --help, -h              Show help message
  --fix-port <port>       FIX TCP port for order entry (default: 12345)
  --port <port>           Alias for --fix-port
  --mcast-group <ip>      Multicast group address (default: 239.255.0.1)
  --mcast-port <port>     Multicast port (default: 12346)
  --no-fix-md             Disable FIX W/X market data on multicast
  --md-sender <compId>    Market data SenderCompID (default: EXCHANGE)
  --md-target <compId>    Market data TargetCompID (default: MD)
  --symbol <symbol>       Trading symbol (default: SYM1)
  --dummy                 Enable dummy order generator
  --dummy-rate <n>        Dummy orders per second (default: 10)
```

### Example

```bash
# Start exchange with dummy order generator at 20 orders/second
./exchange --dummy --dummy-rate 20 --fix-port 12345
```

## Network Protocol

### Order Entry (FIX over TCP)

Clients connect using FIX over TCP to `tcp://<host>:<fix-port>`.

**Message Types**: Logon/Logout/Heartbeat/TestRequest/ResendRequest/SequenceReset plus
`NewOrderSingle (D)`, `OrderCancelRequest (F)`, `OrderCancelReplaceRequest (G)`,
`ExecutionReport (8)`, `OrderCancelReject (9)`.

### Market Data (UDP Multicast)

Clients subscribe to the multicast group to receive market data. The feed includes
the existing binary messages and optional FIX W/X payloads.

| Message Type | Description |
|--------------|-------------|
| `TickUpdate` | Best bid/ask and last trade (binary) |
| `TradeUpdate` | Individual trade details (binary) |
| `OrderbookSnapshot` | Top 5 levels of orderbook (binary) |
| `MarketDataIncrementalRefresh (X)` | FIX incremental updates |
| `MarketDataSnapshotFullRefresh (W)` | FIX snapshot |

## Project Structure

```
Exchange-Matching-Engine/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── include/
│   ├── Protocol.h          # Binary message definitions
│   ├── LockFreeQueue.h     # Thread-safe SPSC/MPSC queues
│   ├── Exchange.h          # Main exchange coordinator
│   ├── MatchingEngine.h    # Order matching logic
│   ├── Orderbook.h         # Price-time priority orderbook
│   ├── Order.h             # Order class
│   ├── FixTcpServer.h      # FIX TCP server
│   ├── SessionManager.h    # Client session tracking
│   ├── FeedHandler.h       # Market data publisher
│   ├── DummyGenerator.h    # Test order generator
│   ├── UdpMulticast.h      # UDP multicast publisher
│   ├── FixMessage.h        # FIX message parsing/serialization
│   ├── Usings.h            # Type aliases
│   └── define.h            # Enums (BuySell, OrderType, etc.)
└── src/
    ├── main.cpp            # Entry point
    ├── Exchange.cpp
    ├── MatchingEngine.cpp
    ├── Orderbook.cpp
    ├── FixTcpServer.cpp
    ├── FeedHandler.cpp
    ├── DummyGenerator.cpp
    └── UdpMulticast.cpp
```

## Client Example (C++)

See `cpp_client/` for a FIX TCP order entry client with multicast market data.

## Threading Model

- **Network Thread**: FIX TCP server event loop, session management
- **Matching Thread**: Single-threaded order matching (deterministic)
- **Feed Thread**: Market data formatting and UDP multicast
- **Dummy Generator Thread**: (Optional) Test order generation

## License

MIT License
