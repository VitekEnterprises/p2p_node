#pragma once
#include "../types.hpp"
#include "../network/udp_socket.hpp"
#include "../network/packet_serializer.hpp"
#include "../util/sha256.hpp"
#include "routing_table.hpp"
#include "xor_distance.hpp"
#include <functional>
#include <map>
#include <random>

namespace p2p {

class DHTProtocol {
public:
    using FoundNodeCallback = std::function<void(const std::vector<NodeInfo>&)>;
    using FoundValueCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    DHTProtocol(UDPSocket& socket, RoutingTable& routingTable, const NodeId& selfId)
        : socket_(socket), routingTable_(routingTable), selfId_(selfId) {}
    
    void handlePacket(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        MessageType type;
        NodeId senderId;
        std::vector<uint8_t> payload;
        
        if (!PacketSerializer::parse(data, len, type, senderId, payload)) {
            Logger::warn("Failed to parse packet");
            return;
        }
        
        NodeInfo senderInfo;
        senderInfo.id = senderId;
        senderInfo.addr = sender;
        senderInfo.lastSeen = std::chrono::steady_clock::now();
        routingTable_.addNode(senderInfo);
        
        switch (type) {
            case MSG_PING:
                handlePing(senderId, sender);
                break;
            case MSG_PONG:
                handlePong(senderId, sender);
                break;
            case MSG_FIND_NODE:
                handleFindNode(senderId, sender, payload);
                break;
            case MSG_FOUND_NODES:
                handleFoundNodes(senderId, sender, payload);
                break;
            case MSG_FIND_VALUE:
                handleFindValue(senderId, sender, payload);
                break;
            case MSG_FOUND_VALUE:
                handleFoundValue(senderId, sender, payload);
                break;
            case MSG_STORE:
                handleStore(senderId, sender, payload);
                break;
            case MSG_STORED:
                handleStored(senderId, sender, payload);
                break;
            case MSG_WHOAMI:
                handleWhoAmI(senderId, sender);
                break;
            case MSG_YOURADDR:
                handleYourAddr(senderId, sender, payload);
                break;
            default:
                Logger::debug("Unknown message type: " + std::to_string(type));
        }
    }
    
    void ping(const sockaddr_in& addr) {
        auto packet = PacketSerializer::createPing(selfId_);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void findNode(const NodeId& target, FoundNodeCallback callback) {
        uint64_t queryId = nextQueryId_++;
        
        PendingQuery query;
        query.type = PendingQuery::FIND_NODE;
        query.target = target;
        query.nodeCallback = callback;
        query.sent = std::chrono::steady_clock::now();
        
        pendingQueries_[queryId] = query;
        
        auto closest = routingTable_.findClosest(target, 3);
        query.closestNodes = closest;
        
        for (const auto& node : closest) {
            sendFindNode(node.addr, target, queryId);
        }
    }
    
    void findValue(const Sha256Hash& hash, FoundValueCallback callback) {
        uint64_t queryId = nextQueryId_++;
        
        PendingQuery query;
        query.type = PendingQuery::FIND_VALUE;
        query.target = hash;
        query.valueCallback = callback;
        query.sent = std::chrono::steady_clock::now();
        
        pendingQueries_[queryId] = query;
        
        auto closest = routingTable_.findClosest(hash, 3);
        query.closestNodes = closest;
        
        for (const auto& node : closest) {
            sendFindValue(node.addr, hash, queryId);
        }
    }
    
    void store(const Sha256Hash& hash, const std::vector<uint8_t>& data) {
        auto closest = routingTable_.findClosest(hash, 8);
        for (const auto& node : closest) {
            auto packet = PacketSerializer::createStore(selfId_, hash, data);
            socket_.sendTo(packet.data(), packet.size(), node.addr);
        }
    }
    
    void refreshBuckets() {
        auto allNodes = routingTable_.getAllNodes();
        for (const auto& node : allNodes) {
            ping(node.addr);
        }
    }
    
    void bootstrap(const std::vector<sockaddr_in>& bootstrapNodes) {
        for (const auto& addr : bootstrapNodes) {
            ping(addr);
            
            NodeId randomTarget;
            for (auto& byte : randomTarget) {
                byte = static_cast<uint8_t>(rand() % 256);
            }
            sendFindNode(addr, randomTarget, 0);
        }
    }

private:
    struct PendingQuery {
        enum Type { FIND_NODE, FIND_VALUE };
        Type type;
        NodeId target;
        std::chrono::steady_clock::time_point sent;
        std::vector<NodeInfo> closestNodes;
        FoundNodeCallback nodeCallback;
        FoundValueCallback valueCallback;
        int attempts{0};
    };
    
    UDPSocket& socket_;
    RoutingTable& routingTable_;
    NodeId selfId_;
    
    std::map<uint64_t, PendingQuery> pendingQueries_;
    uint64_t nextQueryId_{1};
    
    void handlePing(const NodeId& sender, const sockaddr_in& addr) {
        auto packet = PacketSerializer::createPong(selfId_);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void handlePong(const NodeId& sender, const sockaddr_in& addr) {
        NodeInfo node;
        node.id = sender;
        node.addr = addr;
        node.lastSeen = std::chrono::steady_clock::now();
        routingTable_.addNode(node);
    }
    
    void handleFindNode(const NodeId& sender, const sockaddr_in& addr,
                        const std::vector<uint8_t>& payload) {
        if (payload.size() < 32) return;
        
        NodeId target;
        std::copy(payload.begin(), payload.begin() + 32, target.begin());
        
        auto closest = routingTable_.findClosest(target, 8);
        
        closest.erase(std::remove_if(closest.begin(), closest.end(),
                                    [&](const NodeInfo& n) { return n.id == sender; }),
                     closest.end());
        
        auto packet = PacketSerializer::createFoundNodes(selfId_, closest);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void handleFoundNodes(const NodeId& sender, const sockaddr_in& addr,
                          const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        
        uint16_t count = (payload[0] << 8) | payload[1];
        if (payload.size() < 2 + count * (32 + 6)) return;
        
        std::vector<NodeInfo> nodes;
        size_t pos = 2;
        
        for (uint16_t i = 0; i < count; ++i) {
            NodeInfo node;
            
            std::copy(payload.begin() + pos, payload.begin() + pos + 32, node.id.begin());
            pos += 32;
            
            uint32_t ip = (payload[pos] << 24) | (payload[pos+1] << 16) |
                         (payload[pos+2] << 8) | payload[pos+3];
            pos += 4;
            
            uint16_t port = (payload[pos] << 8) | payload[pos+1];
            pos += 2;
            
            node.addr.sin_family = AF_INET;
            node.addr.sin_addr.s_addr = htonl(ip);
            node.addr.sin_port = htons(port);
            node.lastSeen = std::chrono::steady_clock::now();
            
            nodes.push_back(node);
        }
        
        for (const auto& node : nodes) {
            routingTable_.addNode(node);
        }
    }
    
    void handleFindValue(const NodeId& sender, const sockaddr_in& addr,
                         const std::vector<uint8_t>& payload) {
        if (payload.size() < 32) return;
        
        Sha256Hash hash;
        std::copy(payload.begin(), payload.begin() + 32, hash.begin());
        
        auto closest = routingTable_.findClosest(hash, 8);
        auto packet = PacketSerializer::createFoundNodes(selfId_, closest);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void handleFoundValue(const NodeId& sender, const sockaddr_in& addr,
                          const std::vector<uint8_t>& payload) {
        // TODO: Handle found value
    }
    
    void handleStore(const NodeId& sender, const sockaddr_in& addr,
                     const std::vector<uint8_t>& payload) {
        if (payload.size() < 36) return;
        
        Sha256Hash hash;
        std::copy(payload.begin(), payload.begin() + 32, hash.begin());
        
        uint32_t len = (payload[32] << 24) | (payload[33] << 16) |
                       (payload[34] << 8) | payload[35];
        
        if (payload.size() < 36 + len) return;
        
        std::vector<uint8_t> data(payload.begin() + 36, payload.begin() + 36 + len);
        
        auto packet = PacketSerializer::createStored(selfId_, hash);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void handleStored(const NodeId& sender, const sockaddr_in& addr,
                      const std::vector<uint8_t>& payload) {
        if (payload.size() < 32) return;
        
        Sha256Hash hash;
        std::copy(payload.begin(), payload.begin() + 32, hash.begin());
    }
    
    void handleWhoAmI(const NodeId& sender, const sockaddr_in& addr) {
        auto packet = PacketSerializer::createYourAddr(selfId_, addr);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void handleYourAddr(const NodeId& sender, const sockaddr_in& addr,
                        const std::vector<uint8_t>& payload) {
        if (payload.size() < 6) return;
        
        uint32_t ip = (payload[0] << 24) | (payload[1] << 16) |
                      (payload[2] << 8) | payload[3];
        uint16_t port = (payload[4] << 8) | payload[5];
        
        sockaddr_in publicAddr;
        publicAddr.sin_family = AF_INET;
        publicAddr.sin_addr.s_addr = htonl(ip);
        publicAddr.sin_port = htons(port);
    }
    
    void sendFindNode(const sockaddr_in& dest, const NodeId& target, uint64_t queryId) {
        auto packet = PacketSerializer::createFindNode(selfId_, target);
        socket_.sendTo(packet.data(), packet.size(), dest);
    }
    
    void sendFindValue(const sockaddr_in& dest, const Sha256Hash& hash, uint64_t queryId) {
        auto packet = PacketSerializer::createFindValue(selfId_, hash);
        socket_.sendTo(packet.data(), packet.size(), dest);
    }
};

} // namespace p2p
