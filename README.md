# Exchange Matching Engine

A high-performance exchange matching engine with orderbook implementation in C++20. Features ZeroMQ-based client connectivity, UDP multicast market data distribution, and a price-time priority matching algorithm.

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
│ (DEALER) │  ZMQ   │  │   Manager   │    │    Queue     │    │    Handler    │  │
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
- **ZeroMQ Connectivity**: ROUTER/DEALER pattern for client order entry
- **UDP Multicast**: Low-latency market data distribution
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
cd Orderbook

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Run the exchange
./orderbook --dummy
```

### Build Options

```bash
# Enable address/undefined behavior sanitizers
cmake -DENABLE_SANITIZERS=ON ..
```

## Usage

```bash
./orderbook [options]

Options:
  --help, -h              Show help message
  --port <port>           ZMQ ROUTER port for order entry (default: 12345)
  --mcast-group <ip>      Multicast group address (default: 239.255.0.1)
  --mcast-port <port>     Multicast port (default: 12346)
  --symbol <symbol>       Trading symbol (default: SYM1)
  --dummy                 Enable dummy order generator
  --dummy-rate <n>        Dummy orders per second (default: 10)
```

### Example

```bash
# Start exchange with dummy order generator at 20 orders/second
./orderbook --dummy --dummy-rate 20 --port 12345
```

## Network Protocol

### Order Entry (ZeroMQ ROUTER/DEALER)

Clients connect using ZeroMQ DEALER sockets to `tcp://<host>:<port>`.

**Message Format**: Binary packed structs (see `Protocol.h`)

| Message Type | Direction | Description |
|--------------|-----------|-------------|
| `NewOrderRequest` | Client → Exchange | Submit new order |
| `CancelOrderRequest` | Client → Exchange | Cancel existing order |
| `ModifyOrderRequest` | Client → Exchange | Modify existing order |
| `OrderAck` | Exchange → Client | Order accepted |
| `OrderReject` | Exchange → Client | Order rejected |
| `ExecutionReport` | Exchange → Client | Trade execution |
| `CancelAck` | Exchange → Client | Cancel confirmed |
| `ModifyAck` | Exchange → Client | Modify confirmed |

### Market Data (UDP Multicast)

Clients subscribe to the multicast group to receive market data.

| Message Type | Description |
|--------------|-------------|
| `TickUpdate` | Best bid/ask and last trade |
| `TradeUpdate` | Individual trade details |
| `OrderbookSnapshot` | Top 5 levels of orderbook |

## Project Structure

```
Orderbook/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── include/
│   ├── Protocol.h          # Binary message definitions
│   ├── LockFreeQueue.h     # Thread-safe SPSC/MPSC queues
│   ├── Exchange.h          # Main exchange coordinator
│   ├── MatchingEngine.h    # Order matching logic
│   ├── Orderbook.h         # Price-time priority orderbook
│   ├── Order.h             # Order class
│   ├── ZmqServer.h         # ZeroMQ ROUTER server
│   ├── SessionManager.h    # Client session tracking
│   ├── FeedHandler.h       # Market data publisher
│   ├── DummyGenerator.h    # Test order generator
│   ├── UdpMulticast.h      # UDP multicast publisher
│   ├── Usings.h            # Type aliases
│   └── define.h            # Enums (BuySell, OrderType, etc.)
└── src/
    ├── main.cpp            # Entry point
    ├── Exchange.cpp
    ├── MatchingEngine.cpp
    ├── Orderbook.cpp
    ├── ZmqServer.cpp
    ├── FeedHandler.cpp
    ├── DummyGenerator.cpp
    └── UdpMulticast.cpp
```

## Client Example (Python)

```python
import zmq
import struct

# Connect to exchange
context = zmq.Context()
socket = context.socket(zmq.DEALER)
socket.setsockopt(zmq.IDENTITY, b"client1")
socket.connect("tcp://localhost:12345")

# Send a new order (simplified - see Protocol.h for full format)
# ... pack binary message according to NewOrderRequest struct ...

# Receive response
response = socket.recv()
# ... unpack binary response ...
```

## Threading Model

- **Network Thread**: ZMQ server event loop, session management
- **Matching Thread**: Single-threaded order matching (deterministic)
- **Feed Thread**: Market data formatting and UDP multicast
- **Dummy Generator Thread**: (Optional) Test order generation

## License

MIT License
