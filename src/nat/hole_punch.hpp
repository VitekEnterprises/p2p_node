#pragma once
#include "../types.hpp"
#include "../network/udp_socket.hpp"
#include "../network/packet_serializer.hpp"
#include <functional>
#include <map>
#include <chrono>

namespace p2p {

class HolePuncher {
public:
    using PunchCallback = std::function<void(bool success, const sockaddr_in& addr)>;
    
    HolePuncher(UDPSocket& socket, const NodeId& selfId)
        : socket_(socket), selfId_(selfId) {}
    
    void punch(const sockaddr_in& bootstrapAddr,
               const NodeId& targetId,
               PunchCallback callback) {
        
        auto packet = PacketSerializer::createWhoAmI(selfId_);
        socket_.sendTo(packet.data(), packet.size(), bootstrapAddr);
        
        PunchSession session;
        session.targetId = targetId;
        session.callback = callback;
        session.startTime = std::chrono::steady_clock::now();
        session.punching = false;
        
        activePunches_[targetId] = session;
    }
    
    void handlePunchMessage(const uint8_t* data, size_t len, 
                             const sockaddr_in& sender) {
        MessageType type;
        NodeId senderId;
        std::vector<uint8_t> payload;
        
        if (!PacketSerializer::parse(data, len, type, senderId, payload)) {
            return;
        }
        
        if (type == MSG_HOLE_PUNCH) {
            if (payload.size() < 38) return;
            
            NodeId targetId;
            std::copy(payload.begin(), payload.begin() + 32, targetId.begin());
            
            if (targetId == selfId_) {
                // This is for us, punch back
            }
        } else if (type == MSG_YOURADDR) {
            if (payload.size() < 6) return;
            
            uint32_t ip = (payload[0] << 24) | (payload[1] << 16) |
                          (payload[2] << 8) | payload[3];
            uint16_t port = (payload[4] << 8) | payload[5];
            
            publicAddr_.sin_family = AF_INET;
            publicAddr_.sin_addr.s_addr = htonl(ip);
            publicAddr_.sin_port = htons(port);
        }
    }
    
    void setPublicAddress(const sockaddr_in& addr) { publicAddr_ = addr; }
    sockaddr_in getPublicAddress() const { return publicAddr_; }

private:
    struct PunchSession {
        NodeId targetId;
        sockaddr_in targetAddr;
        PunchCallback callback;
        std::chrono::steady_clock::time_point startTime;
        bool punching{false};
    };
    
    UDPSocket& socket_;
    NodeId selfId_;
    sockaddr_in publicAddr_;
    
    std::map<NodeId, PunchSession> activePunches_;
};

} // namespace p2p
