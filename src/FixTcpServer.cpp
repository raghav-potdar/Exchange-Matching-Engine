#include "FixTcpServer.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <ctime>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

namespace {
std::optional<uint64_t> parseUint64(const std::string& value) {
    try {
        size_t idx = 0;
        uint64_t result = std::stoull(value, &idx);
        if (idx != value.size()) {
            return std::nullopt;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int32_t> parseInt32(const std::string& value) {
    try {
        size_t idx = 0;
        int32_t result = std::stol(value, &idx);
        if (idx != value.size()) {
            return std::nullopt;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::string buildResentRaw(const FixMessage& original,
                           const std::string& beginString,
                           const std::string& senderCompId,
                           const std::string& targetCompId,
                           const std::string& origSendingTime,
                           int seqNum) {
    FixMessage resend;
    resend.set(35, original.get(35).value_or("0"));
    resend.set(49, senderCompId);
    resend.set(56, targetCompId);
    resend.set(34, std::to_string(seqNum));
    resend.set(52, origSendingTime);
    resend.set(43, "Y");
    resend.set(122, origSendingTime);

    for (const auto& field : original.fields()) {
        if (field.tag == 8 || field.tag == 9 || field.tag == 10 ||
            field.tag == 35 || field.tag == 49 || field.tag == 56 ||
            field.tag == 34 || field.tag == 52 || field.tag == 43 || field.tag == 122) {
            continue;
        }
        resend.set(field.tag, field.value);
    }

    return resend.serialize(beginString);
}
} // namespace

FixTcpServer::FixTcpServer(uint16_t port, SessionManager& sessionManager, std::string senderCompId)
    : port_(port)
    , sessionManager_(sessionManager)
    , senderCompId_(std::move(senderCompId)) {}

FixTcpServer::~FixTcpServer() {
    stop();
}

bool FixTcpServer::start() {
    if (running_.exchange(true)) {
        return false;
    }

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "FIX TCP server: failed to create socket" << std::endl;
        running_.store(false);
        return false;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "FIX TCP server: failed to bind port " << port_ << std::endl;
        ::close(listenFd_);
        listenFd_ = -1;
        running_.store(false);
        return false;
    }

    if (listen(listenFd_, 64) < 0) {
        std::cerr << "FIX TCP server: listen failed" << std::endl;
        ::close(listenFd_);
        listenFd_ = -1;
        running_.store(false);
        return false;
    }

    std::cout << "FIX TCP server started on 0.0.0.0:" << port_ << std::endl;

    acceptThread_ = std::thread(&FixTcpServer::acceptLoop, this);
    outboundThread_ = std::thread(&FixTcpServer::outboundLoop, this);

    return true;
}

void FixTcpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    if (outboundThread_.joinable()) {
        outboundThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (const auto& [sessionId, session] : sessions_) {
            if (session->socketFd >= 0) {
                ::shutdown(session->socketFd, SHUT_RDWR);
                ::close(session->socketFd);
            }
            sessionManager_.destroySession(sessionId);
        }
        sessions_.clear();
    }

    for (auto& t : clientThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    clientThreads_.clear();

    std::cout << "FIX TCP server stopped" << std::endl;
}

void FixTcpServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        int flag = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        std::string ip = inet_ntoa(clientAddr.sin_addr);
        uint16_t port = ntohs(clientAddr.sin_port);
        std::string connectionId = ip + ":" + std::to_string(port);

        SessionId sessionId = sessionManager_.createSession(connectionId);
        auto session = std::make_shared<SessionState>();
        session->sessionId = sessionId;
        session->socketFd = clientFd;
        session->connectionId = connectionId;

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_[sessionId] = session;
        }

        std::cout << "FIX client connected: " << connectionId << " (session " << sessionId << ")" << std::endl;

        clientThreads_.emplace_back(&FixTcpServer::clientReadLoop, this, session);
    }
}

void FixTcpServer::outboundLoop() {
    while (running_.load()) {
        auto msgOpt = sessionManager_.getOutboundMessage();
        if (!msgOpt.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        OutboundMessage& msg = *msgOpt;
        std::shared_ptr<SessionState> session;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            auto it = sessions_.find(msg.sessionId);
            if (it == sessions_.end()) {
                continue;
            }
            session = it->second;
        }

        if (!session || !session->loggedOn) {
            continue;
        }

        FixMessage fix;
        if (msg.type == MessageType::CANCEL_REJECT || msg.type == MessageType::MODIFY_REJECT) {
            std::string responseTo = (msg.type == MessageType::CANCEL_REJECT) ? "1" : "2";
            fix = buildCancelReject(msg, session, responseTo);
        } else {
            fix = buildExecutionReport(msg, session);
        }
        sendFixMessage(session, fix, true);
    }
}

void FixTcpServer::clientReadLoop(std::shared_ptr<SessionState> session) {
    char buffer[4096];
    while (running_.load()) {
        ssize_t bytesRead = ::recv(session->socketFd, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) {
            break;
        }

        session->readBuffer.append(buffer, static_cast<size_t>(bytesRead));

        while (true) {
            size_t checksumPos = session->readBuffer.find("10=");
            if (checksumPos == std::string::npos) {
                break;
            }
            size_t sohPos = session->readBuffer.find(FixMessage::SOH, checksumPos);
            if (sohPos == std::string::npos) {
                break;
            }

            std::string raw = session->readBuffer.substr(0, sohPos + 1);
            session->readBuffer.erase(0, sohPos + 1);

            std::string error;
            auto msgOpt = FixMessage::parse(raw, error);
            if (!msgOpt.has_value()) {
                std::cerr << "FIX parse error: " << error << std::endl;
                continue;
            }
            handleFixMessage(session, *msgOpt);
        }
    }

    if (session->socketFd >= 0) {
        ::shutdown(session->socketFd, SHUT_RDWR);
        ::close(session->socketFd);
        session->socketFd = -1;
    }

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.erase(session->sessionId);
    }
    sessionManager_.destroySession(session->sessionId);

    std::cout << "FIX client disconnected: " << session->connectionId << std::endl;
}

void FixTcpServer::handleFixMessage(const std::shared_ptr<SessionState>& session, const FixMessage& msg) {
    auto msgTypeOpt = msg.get(35);
    auto seqOpt = msg.get(34);
    auto senderOpt = msg.get(49);
    auto targetOpt = msg.get(56);
    auto sendingTimeOpt = msg.get(52);

    if (!msgTypeOpt || !seqOpt || !senderOpt || !targetOpt || !sendingTimeOpt) {
        int refSeq = seqOpt ? std::stoi(*seqOpt) : 0;
        int refTag = !msgTypeOpt ? 35 : (!seqOpt ? 34 : (!senderOpt ? 49 : (!targetOpt ? 56 : 52)));
        sendSessionReject(session, refSeq, refTag, msgTypeOpt.value_or(""), "Missing required session header");
        return;
    }

    const std::string& msgType = *msgTypeOpt;
    int seqNum = std::stoi(*seqOpt);
    bool possDup = msg.get(43).value_or("N") == "Y";

    if (seqNum > session->nextInboundSeq) {
        sendResendRequest(session, session->nextInboundSeq, seqNum - 1);
        return;
    }
    if (seqNum < session->nextInboundSeq) {
        if (!possDup) {
            sendSessionReject(session, seqNum, 34, msgType, "MsgSeqNum too low");
        }
        return;
    }
    session->nextInboundSeq++;

    if (!session->loggedOn && msgType != "A") {
        return;
    }

    if (msgType == "A" || msgType == "0" || msgType == "1" || msgType == "2" || msgType == "4" || msgType == "5") {
        handleSessionMessage(session, msg, msgType);
        return;
    }

    RejectReason rejectReason = RejectReason::NONE;
    auto inboundOpt = convertInbound(msg, session, rejectReason);
    if (!inboundOpt.has_value()) {
        FixMessage reject;
        reject.set(35, "3");
        reject.set(45, std::to_string(seqNum));
        reject.set(372, msgType);
        reject.set(58, "Invalid order message");
        sendFixMessage(session, reject, false);
        return;
    }

    switch (inboundOpt->type) {
        case MessageType::NEW_ORDER_REQUEST:
            sessionManager_.routeToInbound(inboundOpt->sessionId, inboundOpt->newOrder);
            break;
        case MessageType::CANCEL_ORDER_REQUEST:
            sessionManager_.routeToInbound(inboundOpt->sessionId, inboundOpt->cancelOrder);
            break;
        case MessageType::MODIFY_ORDER_REQUEST:
            sessionManager_.routeToInbound(inboundOpt->sessionId, inboundOpt->modifyOrder);
            break;
        default:
            break;
    }
}

void FixTcpServer::handleSessionMessage(const std::shared_ptr<SessionState>& session, const FixMessage& msg, const std::string& msgType) {
    if (msgType == "A") {
        auto senderOpt = msg.get(49);
        auto targetOpt = msg.get(56);
        if (senderOpt.has_value()) {
            session->clientCompId = *senderOpt;
        }
        if (targetOpt.has_value()) {
            session->targetCompId = *targetOpt;
        }
        session->loggedOn = true;

        FixMessage logon;
        logon.set(35, "A");
        auto enc = msg.get(98);
        auto hb = msg.get(108);
        if (!enc || !hb) {
            sendSessionReject(session, session->nextInboundSeq - 1, !enc ? 98 : 108, "A", "Missing Logon field");
            return;
        }
        logon.set(98, *enc);
        logon.set(108, *hb);
        sendFixMessage(session, logon, true);
        return;
    }

    if (msgType == "0") {
        return;
    }

    if (msgType == "1") {
        FixMessage hb;
        hb.set(35, "0");
        auto testReqId = msg.get(112);
        if (testReqId.has_value()) {
            hb.set(112, *testReqId);
        }
        sendFixMessage(session, hb, false);
        return;
    }

    if (msgType == "2") {
        int beginSeq = msg.get(7).has_value() ? std::stoi(*msg.get(7)) : 1;
        int endSeq = msg.get(16).has_value() ? std::stoi(*msg.get(16)) : 0;
        int highestSeq = 0;
        for (const auto& entry : session->sentMessages) {
            if (entry.first > highestSeq) highestSeq = entry.first;
        }
        if (endSeq == 0 || endSeq > highestSeq) {
            endSeq = highestSeq;
        }
        int seq = beginSeq;
        while (seq <= endSeq) {
            auto it = session->sentMessages.find(seq);
            if (it == session->sentMessages.end()) {
                while (seq <= endSeq && session->sentMessages.find(seq) == session->sentMessages.end()) {
                    ++seq;
                }
                sendGapFill(session, seq);
                continue;
            }
            std::string error;
            auto parsed = FixMessage::parse(it->second.raw, error);
            if (parsed.has_value()) {
                std::string resentRaw = buildResentRaw(*parsed, "FIXT.1.1",
                                                      senderCompId_,
                                                      session->clientCompId.empty() ? "CLIENT" : session->clientCompId,
                                                      it->second.sendingTime,
                                                      seq);
                sendRaw(session, resentRaw);
            } else {
                sendRaw(session, it->second.raw);
            }
            ++seq;
        }
        return;
    }

    if (msgType == "4") {
        auto newSeqNo = msg.get(36);
        if (newSeqNo.has_value()) {
            session->nextInboundSeq = std::stoi(*newSeqNo);
        }
        return;
    }

    if (msgType == "5") {
        FixMessage logout;
        logout.set(35, "5");
        sendFixMessage(session, logout, false);
        ::shutdown(session->socketFd, SHUT_RDWR);
        return;
    }
}

void FixTcpServer::sendFixMessage(const std::shared_ptr<SessionState>& session, FixMessage& msg, bool storeForResend) {
    FixMessage envelope;
    auto msgType = msg.get(35);
    if (!msgType.has_value()) {
        return;
    }
    std::string sendingTime = currentUtcTimestamp();
    envelope.set(35, *msgType);
    envelope.set(49, senderCompId_);
    envelope.set(56, session->clientCompId.empty() ? "CLIENT" : session->clientCompId);
    envelope.set(34, std::to_string(session->nextOutboundSeq));
    envelope.set(52, sendingTime);

    for (const auto& field : msg.fields()) {
        if (field.tag == 35 || field.tag == 49 || field.tag == 56 || field.tag == 34 || field.tag == 52) {
            continue;
        }
        envelope.set(field.tag, field.value);
    }

    std::string raw = envelope.serialize("FIXT.1.1");
    sendRaw(session, raw);
    if (storeForResend) {
        session->sentMessages[session->nextOutboundSeq] = SessionState::SentMessage{raw, sendingTime, *msgType};
    }
    session->nextOutboundSeq++;
}

void FixTcpServer::sendRaw(const std::shared_ptr<SessionState>& session, const std::string& raw) {
    std::lock_guard<std::mutex> lock(session->writeMutex);
    const char* data = raw.data();
    size_t remaining = raw.size();
    while (remaining > 0) {
        ssize_t sent = ::send(session->socketFd, data, remaining, 0);
        if (sent <= 0) {
            break;
        }
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }
}

void FixTcpServer::sendResendRequest(const std::shared_ptr<SessionState>& session, int beginSeq, int endSeq) {
    FixMessage resend;
    resend.set(35, "2");
    resend.set(7, std::to_string(beginSeq));
    resend.set(16, std::to_string(endSeq));
    sendFixMessage(session, resend, false);
}

void FixTcpServer::sendSessionReject(const std::shared_ptr<SessionState>& session,
                                     int refSeqNum,
                                     int refTagId,
                                     const std::string& refMsgType,
                                     const std::string& text) {
    FixMessage reject;
    reject.set(35, "3");
    if (refSeqNum > 0) {
        reject.set(45, std::to_string(refSeqNum));
    }
    if (!refMsgType.empty()) {
        reject.set(372, refMsgType);
    }
    if (refTagId > 0) {
        reject.set(371, std::to_string(refTagId));
    }
    reject.set(58, text);
    sendFixMessage(session, reject, false);
}

void FixTcpServer::sendGapFill(const std::shared_ptr<SessionState>& session, int newSeqNo) {
    FixMessage gapFill;
    gapFill.set(35, "4");
    gapFill.set(123, "Y");
    gapFill.set(36, std::to_string(newSeqNo));
    sendFixMessage(session, gapFill, false);
}

std::optional<InboundMessage> FixTcpServer::convertInbound(
    const FixMessage& msg,
    const std::shared_ptr<SessionState>& session,
    RejectReason& rejectReason) {
    auto msgTypeOpt = msg.get(35);
    if (!msgTypeOpt.has_value()) {
        rejectReason = RejectReason::INVALID_MESSAGE;
        return std::nullopt;
    }

    InboundMessage inbound;
    inbound.sessionId = session->sessionId;

    if (*msgTypeOpt == "D") {
        auto clOrdId = msg.get(11);
        auto side = msg.get(54);
        auto qty = msg.get(38);
        auto ordType = msg.get(40);
        if (!clOrdId || !side || !qty || !ordType) {
            rejectReason = RejectReason::INVALID_MESSAGE;
            return std::nullopt;
        }

        auto clOrdIdNum = parseUint64(*clOrdId);
        auto qtyNum = parseInt32(*qty);
        if (!clOrdIdNum || !qtyNum) {
            rejectReason = RejectReason::INVALID_MESSAGE;
            return std::nullopt;
        }

        uint8_t sideValue = 0;
        if (*side == "1") {
            sideValue = static_cast<uint8_t>(BuySell::BUY);
        } else if (*side == "2") {
            sideValue = static_cast<uint8_t>(BuySell::SELL);
        } else {
            rejectReason = RejectReason::INVALID_SIDE;
            return std::nullopt;
        }

        uint8_t orderTypeValue = 0;
        if (*ordType == "1") {
            orderTypeValue = static_cast<uint8_t>(OrderType::MARKET);
        } else if (*ordType == "2") {
            orderTypeValue = static_cast<uint8_t>(OrderType::LIMIT);
        } else {
            rejectReason = RejectReason::INVALID_ORDER_TYPE;
            return std::nullopt;
        }

        int32_t priceValue = 0;
        if (orderTypeValue == static_cast<uint8_t>(OrderType::LIMIT)) {
            auto price = msg.get(44);
            if (!price) {
                rejectReason = RejectReason::INVALID_PRICE;
                return std::nullopt;
            }
            auto priceNum = parseInt32(*price);
            if (!priceNum) {
                rejectReason = RejectReason::INVALID_PRICE;
                return std::nullopt;
            }
            priceValue = *priceNum;
        }

        inbound.type = MessageType::NEW_ORDER_REQUEST;
        inbound.newOrder.clientOrderId = *clOrdIdNum;
        inbound.newOrder.side = sideValue;
        inbound.newOrder.orderType = orderTypeValue;
        inbound.newOrder.price = priceValue;
        inbound.newOrder.quantity = static_cast<uint32_t>(*qtyNum);
        return inbound;
    }

    if (*msgTypeOpt == "F") {
        auto clOrdId = msg.get(11);
        auto orderId = msg.get(37);
        if (!clOrdId || !orderId) {
            rejectReason = RejectReason::INVALID_MESSAGE;
            return std::nullopt;
        }
        auto clOrdIdNum = parseUint64(*clOrdId);
        auto orderIdNum = parseUint64(*orderId);
        if (!clOrdIdNum || !orderIdNum) {
            rejectReason = RejectReason::INVALID_MESSAGE;
            return std::nullopt;
        }

        inbound.type = MessageType::CANCEL_ORDER_REQUEST;
        inbound.cancelOrder.clientOrderId = *clOrdIdNum;
        inbound.cancelOrder.orderId = *orderIdNum;
        return inbound;
    }

    if (*msgTypeOpt == "G") {
        auto clOrdId = msg.get(11);
        auto orderId = msg.get(37);
        auto qty = msg.get(38);
        auto price = msg.get(44);
        if (!clOrdId || !orderId || !qty) {
            rejectReason = RejectReason::INVALID_MESSAGE;
            return std::nullopt;
        }
        auto clOrdIdNum = parseUint64(*clOrdId);
        auto orderIdNum = parseUint64(*orderId);
        auto qtyNum = parseInt32(*qty);
        if (!clOrdIdNum || !orderIdNum || !qtyNum) {
            rejectReason = RejectReason::INVALID_MESSAGE;
            return std::nullopt;
        }

        int32_t priceValue = 0;
        if (price) {
            auto priceNum = parseInt32(*price);
            if (!priceNum) {
                rejectReason = RejectReason::INVALID_PRICE;
                return std::nullopt;
            }
            priceValue = *priceNum;
        }

        inbound.type = MessageType::MODIFY_ORDER_REQUEST;
        inbound.modifyOrder.clientOrderId = *clOrdIdNum;
        inbound.modifyOrder.orderId = *orderIdNum;
        inbound.modifyOrder.newPrice = priceValue;
        inbound.modifyOrder.newQuantity = static_cast<uint32_t>(*qtyNum);
        return inbound;
    }

    rejectReason = RejectReason::INVALID_MESSAGE;
    return std::nullopt;
}

FixMessage FixTcpServer::buildExecutionReport(const OutboundMessage& msg, const std::shared_ptr<SessionState>& session) {
    (void)session;
    FixMessage fix;
    fix.set(35, "8");

    if (msg.type == MessageType::ORDER_ACK) {
        fix.set(150, "0");
        fix.set(39, "0");
        fix.set(11, std::to_string(msg.orderAck.clientOrderId));
        fix.set(37, std::to_string(msg.orderAck.orderId));
        fix.set(17, std::to_string(msg.orderAck.orderId));
        return fix;
    }

    if (msg.type == MessageType::ORDER_REJECT) {
        fix.set(150, "8");
        fix.set(39, "8");
        fix.set(11, std::to_string(msg.orderReject.clientOrderId));
        fix.set(58, "Order rejected");
        return fix;
    }

    if (msg.type == MessageType::EXECUTION_REPORT) {
        std::string execType = (msg.execReport.leavesQty == 0) ? "2" : "1";
        fix.set(150, execType);
        fix.set(39, execType);
        fix.set(37, std::to_string(msg.execReport.orderId));
        fix.set(17, std::to_string(msg.execReport.tradeId));
        fix.set(31, std::to_string(msg.execReport.execPrice));
        fix.set(32, std::to_string(msg.execReport.execQty));
        fix.set(151, std::to_string(msg.execReport.leavesQty));
        fix.set(14, std::to_string(msg.execReport.execQty));
        fix.set(54, std::to_string(static_cast<int>(msg.execReport.side)));
        return fix;
    }

    if (msg.type == MessageType::CANCEL_ACK) {
        fix.set(150, "4");
        fix.set(39, "4");
        fix.set(11, std::to_string(msg.cancelAck.clientOrderId));
        fix.set(37, std::to_string(msg.cancelAck.orderId));
        return fix;
    }

    if (msg.type == MessageType::MODIFY_ACK) {
        fix.set(150, "5");
        fix.set(39, "5");
        fix.set(11, std::to_string(msg.modifyAck.clientOrderId));
        fix.set(37, std::to_string(msg.modifyAck.orderId));
        fix.set(44, std::to_string(msg.modifyAck.newPrice));
        fix.set(38, std::to_string(msg.modifyAck.newQuantity));
        return fix;
    }

    return fix;
}

FixMessage FixTcpServer::buildCancelReject(const OutboundMessage& msg, const std::shared_ptr<SessionState>& session, const std::string& responseTo) {
    (void)session;
    FixMessage fix;
    fix.set(35, "9");
    if (msg.type == MessageType::CANCEL_REJECT) {
        fix.set(11, std::to_string(msg.cancelReject.clientOrderId));
        fix.set(37, std::to_string(msg.cancelReject.orderId));
    } else {
        fix.set(11, std::to_string(msg.modifyReject.clientOrderId));
        fix.set(37, std::to_string(msg.modifyReject.orderId));
    }
    fix.set(434, responseTo);
    fix.set(102, "1");
    fix.set(58, "Cancel/replace rejected");
    return fix;
}

std::string FixTcpServer::currentUtcTimestamp() {
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
