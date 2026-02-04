#pragma once

#ifndef FIXTCPSERVER_H
#define FIXTCPSERVER_H

#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <optional>

#include "FixMessage.h"
#include "Protocol.h"
#include "SessionManager.h"

class FixTcpServer {
public:
    FixTcpServer(uint16_t port, SessionManager& sessionManager, std::string senderCompId = "EXCHANGE");
    ~FixTcpServer();

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    struct SessionState {
        SessionId sessionId{0};
        int socketFd{-1};
        std::string connectionId;
        std::string clientCompId;
        std::string targetCompId;
        bool loggedOn{false};
        int nextInboundSeq{1};
        int nextOutboundSeq{1};
        struct SentMessage {
            std::string raw;
            std::string sendingTime;
            std::string msgType;
        };
        std::unordered_map<int, SentMessage> sentMessages;
        std::string readBuffer;
        std::mutex writeMutex;
    };

    void acceptLoop();
    void outboundLoop();
    void clientReadLoop(std::shared_ptr<SessionState> session);

    void handleFixMessage(const std::shared_ptr<SessionState>& session, const FixMessage& msg);
    void handleSessionMessage(const std::shared_ptr<SessionState>& session, const FixMessage& msg, const std::string& msgType);

    void sendFixMessage(const std::shared_ptr<SessionState>& session, FixMessage& msg, bool storeForResend);
    void sendRaw(const std::shared_ptr<SessionState>& session, const std::string& raw);
    void sendResendRequest(const std::shared_ptr<SessionState>& session, int beginSeq, int endSeq);
    void sendSessionReject(const std::shared_ptr<SessionState>& session,
                           int refSeqNum,
                           int refTagId,
                           const std::string& refMsgType,
                           const std::string& text);
    void sendGapFill(const std::shared_ptr<SessionState>& session, int newSeqNo);

    std::optional<InboundMessage> convertInbound(const FixMessage& msg, const std::shared_ptr<SessionState>& session, RejectReason& rejectReason);

    FixMessage buildExecutionReport(const OutboundMessage& msg, const std::shared_ptr<SessionState>& session);
    FixMessage buildCancelReject(const OutboundMessage& msg, const std::shared_ptr<SessionState>& session, const std::string& responseTo);

    static std::string currentUtcTimestamp();

private:
    uint16_t port_;
    SessionManager& sessionManager_;
    std::string senderCompId_;

    std::atomic<bool> running_{false};
    int listenFd_{-1};
    std::thread acceptThread_;
    std::thread outboundThread_;
    std::vector<std::thread> clientThreads_;

    std::mutex sessionsMutex_;
    std::unordered_map<SessionId, std::shared_ptr<SessionState>> sessions_;
};

#endif // FIXTCPSERVER_H
