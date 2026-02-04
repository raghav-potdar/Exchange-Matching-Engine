#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Protocol.h"
#include "../include/UdpMulticastReceiver.h"
#include "FixMessage.h"

namespace {

struct Args {
    std::string host = "127.0.0.1";
    uint16_t port = ExchangeConfig::TCP_PORT;
    std::string senderCompId = "CLIENT";
    std::string targetCompId = "EXCHANGE";
    std::string symbol = ExchangeConfig::DEFAULT_SYMBOL;

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
        << "  --port <port>            Exchange FIX TCP port (default: 12345)\n"
        << "  --sender <compId>        SenderCompID (default: CLIENT)\n"
        << "  --target <compId>        TargetCompID (default: EXCHANGE)\n"
        << "  --symbol <symbol>        Symbol (default: SYM1)\n"
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
        else if (arg == "--sender") a.senderCompId = need("--sender");
        else if (arg == "--target") a.targetCompId = need("--target");
        else if (arg == "--symbol") a.symbol = need("--symbol");
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

void printSymbol(const char sym[SYMBOL_LENGTH]) {
    std::string s(sym, sym + SYMBOL_LENGTH);
    auto end = s.find('\0');
    if (end != std::string::npos) s.resize(end);
    std::cout << s;
}

void handleMarketData(const MessageHeader& header, const void* payload, size_t len) {
    if (len >= 2) {
        const char* data = static_cast<const char*>(payload);
        if (data[0] == '8' && data[1] == '=') {
            std::string raw(data, data + len);
            std::string error;
            auto msgOpt = FixMessage::parse(raw, error);
            if (msgOpt.has_value()) {
                const auto& msg = *msgOpt;
                auto msgType = msg.get(35).value_or("");
                if (msgType == "W" || msgType == "X") {
                    std::cout << "[FIX_MD] type=" << msgType
                              << " symbol=" << msg.get(55).value_or("?")
                              << " entries=" << msg.get(268).value_or("?") << "\n";
                    std::string entryAction;
                    std::string entryType;
                    std::string entryPx;
                    std::string entryQty;
                    std::string entryId;
                    std::string entryPos;
                    for (const auto& field : msg.fields()) {
                        if (field.tag == 279) entryAction = field.value;
                        if (field.tag == 269) entryType = field.value;
                        if (field.tag == 270) entryPx = field.value;
                        if (field.tag == 271) entryQty = field.value;
                        if (field.tag == 278) entryId = field.value;
                        if (field.tag == 290) entryPos = field.value;
                        if (!entryType.empty() && !entryPx.empty() && !entryQty.empty()) {
                            std::cout << "  entry action=" << (entryAction.empty() ? "?" : entryAction)
                                      << " type=" << entryType
                                      << " px=" << entryPx
                                      << " qty=" << entryQty;
                            if (!entryId.empty()) {
                                std::cout << " id=" << entryId;
                            }
                            if (!entryPos.empty()) {
                                std::cout << " pos=" << entryPos;
                            }
                            std::cout << "\n";
                            entryAction.clear();
                            entryType.clear();
                            entryPx.clear();
                            entryQty.clear();
                            entryId.clear();
                            entryPos.clear();
                        }
                    }
                    return;
                }
            }
        }
    }

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

std::string currentUtcTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = time_point_cast<seconds>(now);
    auto ms = duration_cast<milliseconds>(now - secs).count();

    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H:%M:%S");
    oss << "." << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

struct FixSession {
    int socketFd{-1};
    std::string senderCompId;
    std::string targetCompId;
    std::atomic<int> nextSeq{1};
    std::mutex writeMutex;
};

bool sendAll(int fd, const std::string& data) {
    const char* ptr = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t sent = ::send(fd, ptr, remaining, 0);
        if (sent <= 0) return false;
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

void sendFixMessage(FixSession& session, FixMessage& msg) {
    FixMessage envelope;
    auto msgType = msg.get(35);
    if (!msgType) return;

    envelope.set(35, *msgType);
    envelope.set(49, session.senderCompId);
    envelope.set(56, session.targetCompId);
    envelope.set(34, std::to_string(session.nextSeq.fetch_add(1)));
    envelope.set(52, currentUtcTimestamp());

    for (const auto& field : msg.fields()) {
        if (field.tag == 35 || field.tag == 49 || field.tag == 56 || field.tag == 34 || field.tag == 52) {
            continue;
        }
        envelope.set(field.tag, field.value);
    }

    std::string raw = envelope.serialize("FIXT.1.1");
    std::lock_guard<std::mutex> lock(session.writeMutex);
    sendAll(session.socketFd, raw);
}

void printFixMessage(const FixMessage& msg) {
    auto msgType = msg.get(35).value_or("?");
    if (msgType == "A") {
        std::cout << "[LOGON] " << "\n";
        return;
    }
    if (msgType == "5") {
        std::cout << "[LOGOUT] " << "\n";
        return;
    }
    if (msgType == "0") {
        std::cout << "[HEARTBEAT] " << "\n";
        return;
    }
    if (msgType == "1") {
        std::cout << "[TEST_REQUEST] " << "\n";
        return;
    }
    if (msgType == "2") {
        std::cout << "[RESEND_REQUEST] " << "\n";
        return;
    }
    if (msgType == "8") {
        std::cout << "[EXECUTION_REPORT] "
                  << "clOrdId=" << msg.get(11).value_or("?")
                  << " orderId=" << msg.get(37).value_or("?")
                  << " execType=" << msg.get(150).value_or("?")
                  << " ordStatus=" << msg.get(39).value_or("?")
                  << " lastQty=" << msg.get(32).value_or("?")
                  << " lastPx=" << msg.get(31).value_or("?")
                  << " leaves=" << msg.get(151).value_or("?")
                  << "\n";
        return;
    }
    if (msgType == "9") {
        std::cout << "[ORDER_CANCEL_REJECT] "
                  << "clOrdId=" << msg.get(11).value_or("?")
                  << " orderId=" << msg.get(37).value_or("?")
                  << " reason=" << msg.get(102).value_or("?")
                  << "\n";
        return;
    }
    std::cout << "[FIX] msgType=" << msgType << "\n";
}

void readFixLoop(FixSession& session, std::atomic<bool>& running) {
    std::string buffer;
    buffer.reserve(4096);
    char temp[4096];
    while (running.load()) {
        ssize_t bytesRead = ::recv(session.socketFd, temp, sizeof(temp), 0);
        if (bytesRead <= 0) {
            break;
        }
        buffer.append(temp, static_cast<size_t>(bytesRead));

        while (true) {
            size_t checksumPos = buffer.find("10=");
            if (checksumPos == std::string::npos) {
                break;
            }
            size_t sohPos = buffer.find(FixMessage::SOH, checksumPos);
            if (sohPos == std::string::npos) {
                break;
            }
            std::string raw = buffer.substr(0, sohPos + 1);
            buffer.erase(0, sohPos + 1);

            std::string error;
            auto msgOpt = FixMessage::parse(raw, error);
            if (!msgOpt.has_value()) {
                std::cout << "[FIX] parse error: " << error << "\n";
                continue;
            }
            printFixMessage(*msgOpt);
        }
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

    // Connect FIX TCP
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(args.port);
    if (inet_pton(AF_INET, args.host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid host\n";
        ::close(sock);
        return 1;
    }
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to FIX server\n";
        ::close(sock);
        return 1;
    }
    std::cout << "Connected FIX TCP to " << args.host << ":" << args.port << "\n";

    FixSession session;
    session.socketFd = sock;
    session.senderCompId = args.senderCompId;
    session.targetCompId = args.targetCompId;

    std::atomic<bool> running{true};
    std::thread reader(readFixLoop, std::ref(session), std::ref(running));

    FixMessage logon;
    logon.set(35, "A");
    logon.set(98, "0");
    logon.set(108, "30");
    sendFixMessage(session, logon);

    if (args.demo) {
        FixMessage order;
        order.set(35, "D");
        order.set(11, "1");
        order.set(55, args.symbol);
        order.set(54, (args.demoSide == "buy") ? "1" : "2");
        order.set(40, (args.demoType == "market") ? "1" : "2");
        order.set(38, std::to_string(args.demoQty));
        if (args.demoType != "market") {
            order.set(44, std::to_string(args.demoPrice));
        }
        sendFixMessage(session, order);
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
            FixMessage order;
            order.set(35, "D");
            order.set(11, std::to_string(clientOrderId));
            order.set(55, args.symbol);
            order.set(54, (sideS == "buy") ? "1" : "2");
            order.set(40, (typeS == "market") ? "1" : "2");
            order.set(38, std::to_string(qty));
            if (typeS != "market") {
                order.set(44, std::to_string(price));
            }
            sendFixMessage(session, order);
        } else if (cmd == "cancel") {
            uint64_t clientOrderId = 0;
            uint64_t orderId = 0;
            if (!(iss >> clientOrderId >> orderId)) {
                std::cout << "Usage: cancel <clientOrderId> <orderId>\n";
                continue;
            }
            FixMessage cancel;
            cancel.set(35, "F");
            cancel.set(11, std::to_string(clientOrderId));
            cancel.set(37, std::to_string(orderId));
            cancel.set(55, args.symbol);
            sendFixMessage(session, cancel);
        } else if (cmd == "modify") {
            uint64_t clientOrderId = 0;
            uint64_t orderId = 0;
            int32_t newPx = 0;
            uint32_t newQty = 0;
            if (!(iss >> clientOrderId >> orderId >> newPx >> newQty)) {
                std::cout << "Usage: modify <clientOrderId> <orderId> <newPrice> <newQty>\n";
                continue;
            }
            FixMessage replace;
            replace.set(35, "G");
            replace.set(11, std::to_string(clientOrderId));
            replace.set(37, std::to_string(orderId));
            replace.set(55, args.symbol);
            replace.set(38, std::to_string(newQty));
            replace.set(44, std::to_string(newPx));
            sendFixMessage(session, replace);
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
            continue;
        }
    }

    running.store(false);
    ::shutdown(session.socketFd, SHUT_RDWR);
    ::close(session.socketFd);
    if (reader.joinable()) {
        reader.join();
    }

    mcast.stop();
    return 0;
}

