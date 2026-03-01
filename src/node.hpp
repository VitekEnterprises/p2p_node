#pragma once
#include "network/udp_socket.hpp"
#include "network/packet_serializer.hpp"
#include "dht/routing_table.hpp"
#include "dht/dht_protocol.hpp"
#include "nat/hole_punch.hpp"
#include "storage/block_store.hpp"
#include "util/sha256.hpp"
#include "util/random.hpp"
#include <thread>
#include <atomic>
#include <fstream>

namespace p2p {

class P2PNode {
public:
    P2PNode(uint16_t port, const std::string& storagePath)
        : port_(port),
          socket_(port),
          nodeId_(loadOrCreateNodeId()),
          routingTable_(nodeId_),
          dht_(socket_, routingTable_, nodeId_),
          holePuncher_(socket_, nodeId_),
          blockStore_(storagePath) {}
    
    ~P2PNode() {
        stop();
    }
    
    bool init() {
        Random::init();
        
        if (!socket_.init()) {
            Logger::error("Failed to initialize socket");
            return false;
        }
        
        socket_.setPacketHandler([this](const uint8_t* data, size_t len, 
                                         const sockaddr_in& sender) {
            onPacket(data, len, sender);
        });
        
        Logger::info("Node initialized with ID: " + SHA256::toHex(nodeId_));
        return true;
    }
    
    void start() {
        socket_.startReceiveLoop();
        running_ = true;
        maintenanceThread_ = std::thread(&P2PNode::maintenanceLoop, this);
        Logger::info("Node started");
    }
    
    void stop() {
        running_ = false;
        socket_.stop();
        if (maintenanceThread_.joinable()) {
            maintenanceThread_.join();
        }
        Logger::info("Node stopped");
    }
    
    void bootstrap(const std::vector<std::string>& bootstrapNodes) {
        std::vector<sockaddr_in> addrs;
        
        for (const auto& node : bootstrapNodes) {
            size_t colonPos = node.find(':');
            if (colonPos == std::string::npos) continue;
            
            std::string ip = node.substr(0, colonPos);
            uint16_t port = std::stoi(node.substr(colonPos + 1));
            
            sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            
#ifdef _WIN32
            if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
#else
            if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
#endif
                Logger::warn("Invalid bootstrap address: " + node);
                continue;
            }
            
            addrs.push_back(addr);
        }
        
        dht_.bootstrap(addrs);
        
        for (const auto& addr : addrs) {
            auto packet = PacketSerializer::createWhoAmI(nodeId_);
            socket_.sendTo(packet.data(), packet.size(), addr);
        }
    }
    
    bool shareFile(const std::string& filepath) {
        FileMetadata metadata;
        return blockStore_.storeFile(filepath, metadata);
    }
    
    void downloadFile(const Sha256Hash& fileHash, const std::string& savePath) {
        dht_.findValue(fileHash, [this, fileHash, savePath](const std::vector<uint8_t>& data) {
            if (data.empty()) {
                Logger::warn("File not found");
                return;
            }
            
            std::string output;
            if (blockStore_.loadFile(fileHash, output)) {
                Logger::info("File downloaded to: " + output);
            }
        });
    }
    
    NodeId getNodeId() const { return nodeId_; }
    RoutingTable& getRoutingTable() { return routingTable_; }

private:
    NodeId nodeId_;
    uint16_t port_;
    
    UDPSocket socket_;
    RoutingTable routingTable_;
    DHTProtocol dht_;
    HolePuncher holePuncher_;
    BlockStore blockStore_;
    
    std::atomic<bool> running_{false};
    std::thread maintenanceThread_;
    
    NodeId loadOrCreateNodeId() {
        std::ifstream file("node.id", std::ios::binary);
        NodeId id;
        
        if (file) {
            file.read(reinterpret_cast<char*>(id.data()), 32);
            if (file.gcount() == 32) {
                Logger::info("Loaded existing node ID");
                return id;
            }
        }
        
        id = Random::generateNodeId();
        std::ofstream out("node.id", std::ios::binary);
        out.write(reinterpret_cast<const char*>(id.data()), 32);
        Logger::info("Generated new node ID");
        return id;
    }
    
    void maintenanceLoop() {
        auto lastRefresh = std::chrono::steady_clock::now();
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            auto now = std::chrono::steady_clock::now();
            if (now - lastRefresh > std::chrono::minutes(15)) {
                dht_.refreshBuckets();
                lastRefresh = now;
            }
        }
    }
    
    void onPacket(const uint8_t* data, size_t len, const sockaddr_in& sender) {
        dht_.handlePacket(data, len, sender);
        holePuncher_.handlePunchMessage(data, len, sender);
        
        MessageType type;
        NodeId senderId;
        std::vector<uint8_t> payload;
        
        if (PacketSerializer::parse(data, len, type, senderId, payload)) {
            if (type == MSG_REQUEST_BLOCK) {
                handleBlockRequest(senderId, sender, payload);
            } else if (type == MSG_SEND_BLOCK) {
                handleBlockSend(senderId, sender, payload);
            }
        }
    }
    
    void handleBlockRequest(const NodeId& senderId, const sockaddr_in& sender,
                            const std::vector<uint8_t>& payload) {
        if (payload.size() < 36) return;
        
        Sha256Hash fileHash;
        std::copy(payload.begin(), payload.begin() + 32, fileHash.begin());
        
        uint32_t blockIndex = (payload[32] << 24) | (payload[33] << 16) |
                              (payload[34] << 8) | payload[35];
        
        std::vector<uint8_t> dummyData(1024, 0);
        auto packet = PacketSerializer::createSendBlock(nodeId_, fileHash, 
                                                        blockIndex, dummyData);
        socket_.sendTo(packet.data(), packet.size(), sender);
    }
    
    void handleBlockSend(const NodeId& senderId, const sockaddr_in& sender,
                         const std::vector<uint8_t>& payload) {
        if (payload.size() < 40) return;
        
        Sha256Hash fileHash;
        std::copy(payload.begin(), payload.begin() + 32, fileHash.begin());
        
        uint32_t blockIndex = (payload[32] << 24) | (payload[33] << 16) |
                              (payload[34] << 8) | payload[35];
        
        uint32_t dataLen = (payload[36] << 24) | (payload[37] << 16) |
                           (payload[38] << 8) | payload[39];
        
        if (payload.size() < 40 + dataLen) return;
        
        std::vector<uint8_t> data(payload.begin() + 40, payload.begin() + 40 + dataLen);
        
        auto computedHash = SHA256::hash(data);
        blockStore_.storeBlock(computedHash, data.data(), data.size());
    }
};

} // namespace p2p
