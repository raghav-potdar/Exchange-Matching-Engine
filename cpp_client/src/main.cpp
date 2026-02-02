#include <zmq.hpp>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "Protocol.h"
#include "UdpMulticastReceiver.h"
#include "define.h"

namespace {

struct Args {
    std::string host = "127.0.0.1";
    uint16_t port = ExchangeConfig::TCP_PORT;
    std::string identity = "client1";

    std::string mcastGroup = ExchangeConfig::MULTICAST_GROUP;
    uint16_t mcastPort = ExchangeConfig::MULTICAST_PORT;

    bool demo = false;
    std::string demoSide = "buy";
    std::string demoType = "limit";
    int32_t demoPrice = 1000;
    uint32_t demoQty = 10;
};

void usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n\n"
        << "Options:\n"
        << "  --host <ip>              Exchange host (default: 127.0.0.1)\n"
        << "  --port <port>            Exchange ZMQ port (default: 12345)\n"
        << "  --id <identity>          ZMQ identity (default: client1)\n"
        << "  --mcast-group <ip>       Multicast group (default: 239.255.0.1)\n"
        << "  --mcast-port <port>      Multicast port (default: 12346)\n"
        << "  --demo                   Send a demo order on startup\n"
        << "  --side <buy|sell>         Demo side\n"
        << "  --type <limit|market|ioc> Demo order type\n"
        << "  --price <px>              Demo price (limit/ioc)\n"
        << "  --qty <qty>               Demo quantity\n"
        << "  --help, -h               Show this help\n";
}

std::optional<Args> parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return std::nullopt;
        }
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--host") a.host = need("--host");
        else if (arg == "--port") a.port = static_cast<uint16_t>(std::atoi(need("--port")));
        else if (arg == "--id") a.identity = need("--id");
        else if (arg == "--mcast-group") a.mcastGroup = need("--mcast-group");
        else if (arg == "--mcast-port") a.mcastPort = static_cast<uint16_t>(std::atoi(need("--mcast-port")));
        else if (arg == "--demo") a.demo = true;
        else if (arg == "--side") a.demoSide = need("--side");
        else if (arg == "--type") a.demoType = need("--type");
        else if (arg == "--price") a.demoPrice = static_cast<int32_t>(std::atoi(need("--price")));
        else if (arg == "--qty") a.demoQty = static_cast<uint32_t>(std::atoi(need("--qty")));
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            usage(argv[0]);
            std::exit(2);
        }
    }
    return a;
}

uint8_t parseSide(const std::string& s) {
    if (s == "buy") return static_cast<uint8_t>(BuySell::BUY);
    if (s == "sell") return static_cast<uint8_t>(BuySell::SELL);
    return static_cast<uint8_t>(BuySell::DEFAULT);
}

uint8_t parseOrderType(const std::string& s) {
    if (s == "limit") return static_cast<uint8_t>(OrderType::LIMIT);
    if (s == "market") return static_cast<uint8_t>(OrderType::MARKET);
    if (s == "ioc") return static_cast<uint8_t>(OrderType::IOC);
    return static_cast<uint8_t>(OrderType::DEFAULT);
}

void printSymbol(const char sym[SYMBOL_LENGTH]) {
    std::string s(sym, sym + SYMBOL_LENGTH);
    auto end = s.find('\0');
    if (end != std::string::npos) s.resize(end);
    std::cout << s;
}

void handleMarketData(const MessageHeader& header, const void* payload, size_t len) {
    (void)len;
    switch (header.type) {
        case MessageType::TICK_UPDATE: {
            if (len < sizeof(TickUpdate)) return;
            const auto* t = reinterpret_cast<const TickUpdate*>(payload);
            std::cout << "[TICK] ";
            printSymbol(t->symbol);
            std::cout << " last=" << t->lastPrice << "@" << t->lastQty
                      << " bid=" << t->bidPrice << "@" << t->bidQty
                      << " ask=" << t->askPrice << "@" << t->askQty
                      << "\n";
            break;
        }
        case MessageType::TRADE_UPDATE: {
            if (len < sizeof(TradeUpdate)) return;
            const auto* t = reinterpret_cast<const TradeUpdate*>(payload);
            std::cout << "[TRADE] ";
            printSymbol(t->symbol);
            std::cout << " tradeId=" << t->tradeId
                      << " px=" << t->price
                      << " qty=" << t->quantity
                      << " aggressor=" << static_cast<int>(t->aggressorSide)
                      << "\n";
            break;
        }
        case MessageType::ORDERBOOK_SNAPSHOT: {
            if (len < sizeof(OrderbookSnapshot)) return;
            const auto* s = reinterpret_cast<const OrderbookSnapshot*>(payload);
            std::cout << "[SNAP] ";
            printSymbol(s->symbol);
            std::cout << " bids=" << static_cast<int>(s->bidLevels)
                      << " asks=" << static_cast<int>(s->askLevels) << "\n";
            break;
        }
        default:
            break;
    }
}

// Server expects ROUTER recv frames: [identity][empty][payload]
// So DEALER must send multipart: [empty][payload]
template <typename T>
void zmqSendStruct(zmq::socket_t& dealer, const T& msg) {
    zmq::message_t empty(0);
    dealer.send(empty, zmq::send_flags::sndmore);
    zmq::message_t payload(&msg, sizeof(T));
    dealer.send(payload, zmq::send_flags::none);
}

// Server sends ROUTER frames: [identity][empty][payload]
// DEALER typically receives: [empty][payload]
std::optional<zmq::message_t> zmqRecvPayload(zmq::socket_t& dealer) {
    zmq::message_t f1;
    if (!dealer.recv(f1, zmq::recv_flags::none)) return std::nullopt;
    bool more = dealer.get(zmq::sockopt::rcvmore);
    if (!more) {
        // Some stacks may deliver just payload; treat f1 as payload.
        return f1;
    }
    zmq::message_t f2;
    if (!dealer.recv(f2, zmq::recv_flags::none)) return std::nullopt;
    return f2;
}

void printResponse(const zmq::message_t& payload) {
    if (payload.size() < sizeof(MessageHeader)) {
        std::cout << "[ZMQ] Response too small: " << payload.size() << " bytes\n";
        return;
    }
    const auto* hdr = static_cast<const MessageHeader*>(payload.data());
    switch (hdr->type) {
        case MessageType::ORDER_ACK: {
            if (payload.size() < sizeof(OrderAck)) return;
            const auto* m = static_cast<const OrderAck*>(payload.data());
            std::cout << "[ORDER_ACK] clientOrderId=" << m->clientOrderId
                      << " orderId=" << m->orderId
                      << " status=" << static_cast<int>(m->status) << "\n";
            break;
        }
        case MessageType::ORDER_REJECT: {
            if (payload.size() < sizeof(OrderReject)) return;
            const auto* m = static_cast<const OrderReject*>(payload.data());
            std::cout << "[ORDER_REJECT] clientOrderId=" << m->clientOrderId
                      << " reason=" << static_cast<int>(m->reason) << "\n";
            break;
        }
        case MessageType::EXECUTION_REPORT: {
            if (payload.size() < sizeof(ExecutionReport)) return;
            const auto* m = static_cast<const ExecutionReport*>(payload.data());
            std::cout << "[EXEC] orderId=" << m->orderId
                      << " tradeId=" << m->tradeId
                      << " px=" << m->execPrice
                      << " qty=" << m->execQty
                      << " leaves=" << m->leavesQty
                      << " side=" << static_cast<int>(m->side) << "\n";
            break;
        }
        case MessageType::CANCEL_ACK: {
            if (payload.size() < sizeof(CancelAck)) return;
            const auto* m = static_cast<const CancelAck*>(payload.data());
            std::cout << "[CANCEL_ACK] clientOrderId=" << m->clientOrderId
                      << " orderId=" << m->orderId << "\n";
            break;
        }
        case MessageType::CANCEL_REJECT: {
            if (payload.size() < sizeof(CancelReject)) return;
            const auto* m = static_cast<const CancelReject*>(payload.data());
            std::cout << "[CANCEL_REJECT] clientOrderId=" << m->clientOrderId
                      << " orderId=" << m->orderId
                      << " reason=" << static_cast<int>(m->reason) << "\n";
            break;
        }
        case MessageType::MODIFY_ACK: {
            if (payload.size() < sizeof(ModifyAck)) return;
            const auto* m = static_cast<const ModifyAck*>(payload.data());
            std::cout << "[MODIFY_ACK] clientOrderId=" << m->clientOrderId
                      << " orderId=" << m->orderId
                      << " px=" << m->newPrice
                      << " qty=" << m->newQuantity << "\n";
            break;
        }
        case MessageType::MODIFY_REJECT: {
            if (payload.size() < sizeof(ModifyReject)) return;
            const auto* m = static_cast<const ModifyReject*>(payload.data());
            std::cout << "[MODIFY_REJECT] clientOrderId=" << m->clientOrderId
                      << " orderId=" << m->orderId
                      << " reason=" << static_cast<int>(m->reason) << "\n";
            break;
        }
        default:
            std::cout << "[ZMQ] Unknown msg type=" << static_cast<int>(hdr->type)
                      << " len=" << hdr->length << "\n";
            break;
    }
}

}  // namespace

int main(int argc, char** argv) {
    auto argsOpt = parseArgs(argc, argv);
    if (!argsOpt) return 0;
    Args args = *argsOpt;

    // Start multicast receiver (prints ticks/trades/snapshots)
    UdpMulticastReceiver mcast(args.mcastGroup, args.mcastPort);
    if (!mcast.start(handleMarketData)) {
        std::cerr << "Failed to start multicast receiver\n";
        return 1;
    }
    std::cout << "Listening multicast " << args.mcastGroup << ":" << args.mcastPort << "\n";

    // Connect ZMQ DEALER
    zmq::context_t ctx(1);
    zmq::socket_t dealer(ctx, zmq::socket_type::dealer);
    dealer.set(zmq::sockopt::linger, 0);
    // cppzmq uses "routing_id" for ZMQ_IDENTITY / ZMQ_ROUTING_ID
    dealer.set(zmq::sockopt::routing_id, zmq::buffer(args.identity));

    std::string endpoint = "tcp://" + args.host + ":" + std::to_string(args.port);
    dealer.connect(endpoint);
    std::cout << "Connected ZMQ DEALER to " << endpoint << " as id=" << args.identity << "\n";

    if (args.demo) {
        NewOrderRequest req{};
        req.clientOrderId = 1;
        req.side = parseSide(args.demoSide);
        req.orderType = parseOrderType(args.demoType);
        req.price = args.demoPrice;
        req.quantity = args.demoQty;
        zmqSendStruct(dealer, req);

        if (auto resp = zmqRecvPayload(dealer)) {
            printResponse(*resp);
        } else {
            std::cout << "[ZMQ] No response\n";
        }
    }

    std::cout << "Enter commands: new/cancel/modify/quit\n";
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;
        if (cmd == "quit" || cmd == "exit") break;

        if (cmd == "new") {
            uint64_t clientOrderId = 0;
            std::string sideS, typeS;
            int32_t price = 0;
            uint32_t qty = 0;
            if (!(iss >> clientOrderId >> sideS >> typeS >> price >> qty)) {
                std::cout << "Usage: new <clientOrderId> <buy|sell> <limit|market|ioc> <price> <qty>\n";
                continue;
            }
            NewOrderRequest req{};
            req.clientOrderId = clientOrderId;
            req.side = parseSide(sideS);
            req.orderType = parseOrderType(typeS);
            req.price = price;
            req.quantity = qty;
            zmqSendStruct(dealer, req);
        } else if (cmd == "cancel") {
            uint64_t clientOrderId = 0;
            uint64_t orderId = 0;
            if (!(iss >> clientOrderId >> orderId)) {
                std::cout << "Usage: cancel <clientOrderId> <orderId>\n";
                continue;
            }
            CancelOrderRequest req{};
            req.clientOrderId = clientOrderId;
            req.orderId = orderId;
            zmqSendStruct(dealer, req);
        } else if (cmd == "modify") {
            uint64_t clientOrderId = 0;
            uint64_t orderId = 0;
            int32_t newPx = 0;
            uint32_t newQty = 0;
            if (!(iss >> clientOrderId >> orderId >> newPx >> newQty)) {
                std::cout << "Usage: modify <clientOrderId> <orderId> <newPrice> <newQty>\n";
                continue;
            }
            ModifyOrderRequest req{};
            req.clientOrderId = clientOrderId;
            req.orderId = orderId;
            req.newPrice = newPx;
            req.newQuantity = newQty;
            zmqSendStruct(dealer, req);
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
            continue;
        }

        // Read 1 response synchronously (simple client behavior)
        if (auto resp = zmqRecvPayload(dealer)) {
            printResponse(*resp);
        } else {
            std::cout << "[ZMQ] No response\n";
        }
    }

    mcast.stop();
    return 0;
}

