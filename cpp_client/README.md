# Orderbook C++ Client

This is a standalone C++20 client application that:

- Connects to the exchange via **FIX over TCP** (order entry)
- Subscribes to **UDP multicast** (market data)

It reuses the protocol structs and FIX helpers from the exchange project (`../include/Protocol.h`, `../include/FixMessage.h`).

## Build

```bash
cd cpp_client
mkdir -p build && cd build
cmake ..
cmake --build . -j
```

## Run

### Basic (connect + listen to multicast)

```bash
./orderbook_client --host 127.0.0.1 --port 12345 --mcast-group 239.255.0.1 --mcast-port 12346 --sender CLIENT --target EXCHANGE
```

The client prints both binary market data and FIX W/X messages if the exchange is configured to publish them.

### Send a demo order on startup

```bash
./orderbook_client --demo --side buy --type limit --price 1000 --qty 10
```

### Interactive commands

Once running, type:

- `new <clientOrderId> <buy|sell> <limit|market|ioc> <price> <qty>`
- `cancel <clientOrderId> <orderId>`
- `modify <clientOrderId> <orderId> <newPrice> <newQty>`
- `quit`

