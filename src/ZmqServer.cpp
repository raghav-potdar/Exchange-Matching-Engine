#include "ZmqServer.h"

#include <iostream>
#include <cstring>

ZmqServer::ZmqServer(uint16_t port, SessionManager& sessionManager)
    : port_(port)
    , sessionManager_(sessionManager)
{
}

ZmqServer::~ZmqServer() {
    stop();
}

bool ZmqServer::start() {
    if (running_.exchange(true)) {
        return false;  // Already running
    }
    
    try {
        // Create ZMQ context
        context_ = std::make_unique<zmq::context_t>(1);
        
        // Create ROUTER socket
        socket_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::router);
        
        // Set socket options
        socket_->set(zmq::sockopt::linger, 0);  // Don't block on close
        socket_->set(zmq::sockopt::rcvtimeo, POLL_TIMEOUT_MS);  // Receive timeout
        
        // Bind to port
        std::string endpoint = "tcp://*:" + std::to_string(port_);
        socket_->bind(endpoint);
        
        std::cout << "ZMQ Server started on " << endpoint << std::endl;
        
        // Start server thread
        serverThread_ = std::thread(&ZmqServer::eventLoop, this);
        
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ Server failed to start: " << e.what() << std::endl;
        running_.store(false);
        return false;
    }
}

void ZmqServer::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    // Wait for server thread to finish
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    // Disconnect all sessions
    for (const auto& [identity, sessionId] : identityToSession_) {
        sessionManager_.destroySession(sessionId);
    }
    identityToSession_.clear();
    sessionToIdentity_.clear();
    
    // Close socket and context
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    if (context_) {
        context_->close();
        context_.reset();
    }
    
    std::cout << "ZMQ Server stopped" << std::endl;
}

void ZmqServer::eventLoop() {
    while (running_.load()) {
        try {
            // Process incoming messages from clients
            processIncoming();
            
            // Process outbound messages to clients
            processOutbound();
            
        } catch (const zmq::error_t& e) {
            if (running_.load()) {
                std::cerr << "ZMQ error in event loop: " << e.what() << std::endl;
            }
        }
    }
}

void ZmqServer::processIncoming() {
    // ROUTER socket receives: [identity][empty delimiter][message]
    std::vector<zmq::message_t> frames;
    zmq::recv_result_t result;
    
    // Try to receive identity frame
    zmq::message_t identityFrame;
    result = socket_->recv(identityFrame, zmq::recv_flags::dontwait);
    
    if (!result.has_value() || *result == 0) {
        return;  // No message available
    }
    
    // Extract identity as string
    std::string identity(static_cast<char*>(identityFrame.data()), identityFrame.size());
    
    // Receive empty delimiter frame
    zmq::message_t delimiterFrame;
    result = socket_->recv(delimiterFrame, zmq::recv_flags::none);
    if (!result.has_value()) {
        std::cerr << "Failed to receive delimiter frame" << std::endl;
        return;
    }
    
    // Receive message frame
    zmq::message_t messageFrame;
    result = socket_->recv(messageFrame, zmq::recv_flags::none);
    if (!result.has_value()) {
        std::cerr << "Failed to receive message frame" << std::endl;
        return;
    }
    
    // Check if this is a new client
    auto it = identityToSession_.find(identity);
    if (it == identityToSession_.end()) {
        // New client - create session
        SessionId sessionId = sessionManager_.createSession(identity);
        identityToSession_[identity] = sessionId;
        sessionToIdentity_[sessionId] = identity;
        
        std::cout << "Client connected: identity=" << identity.size() << " bytes"
                  << " (session " << sessionId << ")" << std::endl;
    }
    
    // Parse and route the message
    parseAndRouteMessage(identity, messageFrame);
}

bool ZmqServer::parseAndRouteMessage(const std::string& identity, const zmq::message_t& msg) {
    if (msg.size() < sizeof(MessageHeader)) {
        std::cerr << "Message too small for header" << std::endl;
        return false;
    }
    
    auto it = identityToSession_.find(identity);
    if (it == identityToSession_.end()) {
        std::cerr << "Unknown client identity" << std::endl;
        return false;
    }
    
    SessionId sessionId = it->second;
    const MessageHeader* header = static_cast<const MessageHeader*>(msg.data());
    
    // Validate message length
    if (msg.size() < header->length) {
        std::cerr << "Message truncated" << std::endl;
        return false;
    }
    
    bool success = false;
    
    switch (header->type) {
        case MessageType::NEW_ORDER_REQUEST: {
            if (msg.size() >= sizeof(NewOrderRequest)) {
                const NewOrderRequest* request = static_cast<const NewOrderRequest*>(msg.data());
                success = sessionManager_.routeToInbound(sessionId, *request);
            }
            break;
        }
        case MessageType::CANCEL_ORDER_REQUEST: {
            if (msg.size() >= sizeof(CancelOrderRequest)) {
                const CancelOrderRequest* request = static_cast<const CancelOrderRequest*>(msg.data());
                success = sessionManager_.routeToInbound(sessionId, *request);
            }
            break;
        }
        case MessageType::MODIFY_ORDER_REQUEST: {
            if (msg.size() >= sizeof(ModifyOrderRequest)) {
                const ModifyOrderRequest* request = static_cast<const ModifyOrderRequest*>(msg.data());
                success = sessionManager_.routeToInbound(sessionId, *request);
            }
            break;
        }
        default:
            std::cerr << "Unknown message type: " << static_cast<int>(header->type) << std::endl;
            break;
    }
    
    return success;
}

void ZmqServer::processOutbound() {
    // Process outbound messages from the queue
    while (auto msgOpt = sessionManager_.getOutboundMessage()) {
        OutboundMessage& msg = *msgOpt;
        
        // Find client identity for this session
        auto it = sessionToIdentity_.find(msg.sessionId);
        if (it == sessionToIdentity_.end()) {
            // Session no longer exists (client disconnected)
            continue;
        }
        
        const std::string& identity = it->second;
        const void* data = nullptr;
        size_t length = 0;
        
        switch (msg.type) {
            case MessageType::ORDER_ACK:
                data = &msg.orderAck;
                length = sizeof(OrderAck);
                break;
            case MessageType::ORDER_REJECT:
                data = &msg.orderReject;
                length = sizeof(OrderReject);
                break;
            case MessageType::EXECUTION_REPORT:
                data = &msg.execReport;
                length = sizeof(ExecutionReport);
                break;
            case MessageType::CANCEL_ACK:
                data = &msg.cancelAck;
                length = sizeof(CancelAck);
                break;
            case MessageType::CANCEL_REJECT:
                data = &msg.cancelReject;
                length = sizeof(CancelReject);
                break;
            case MessageType::MODIFY_ACK:
                data = &msg.modifyAck;
                length = sizeof(ModifyAck);
                break;
            case MessageType::MODIFY_REJECT:
                data = &msg.modifyReject;
                length = sizeof(ModifyReject);
                break;
            default:
                continue;
        }
        
        if (data && length > 0) {
            sendToClient(identity, data, length);
        }
    }
}

bool ZmqServer::sendToClient(const std::string& identity, const void* data, size_t length) {
    try {
        // ROUTER socket sends: [identity][empty delimiter][message]
        
        // Send identity frame
        zmq::message_t identityFrame(identity.data(), identity.size());
        socket_->send(identityFrame, zmq::send_flags::sndmore);
        
        // Send empty delimiter frame
        zmq::message_t delimiterFrame(0);
        socket_->send(delimiterFrame, zmq::send_flags::sndmore);
        
        // Send message frame
        zmq::message_t messageFrame(data, length);
        socket_->send(messageFrame, zmq::send_flags::none);
        
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to send to client: " << e.what() << std::endl;
        return false;
    }
}

void ZmqServer::handleClientDisconnect(const std::string& identity) {
    auto it = identityToSession_.find(identity);
    if (it != identityToSession_.end()) {
        SessionId sessionId = it->second;
        sessionManager_.destroySession(sessionId);
        sessionToIdentity_.erase(sessionId);
        identityToSession_.erase(it);
        
        std::cout << "Client disconnected (session " << sessionId << ")" << std::endl;
    }
}
