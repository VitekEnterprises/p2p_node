#pragma once
#include "../types.hpp"
#include <vector>

namespace p2p {

enum MessageType : uint8_t {
    MSG_PING = 0x01,
    MSG_PONG = 0x02,
    MSG_FIND_NODE = 0x03,
    MSG_FOUND_NODES = 0x04,
    MSG_FIND_VALUE = 0x05,
    MSG_FOUND_VALUE = 0x06,
    MSG_STORE = 0x07,
    MSG_STORED = 0x08,
    MSG_WHOAMI = 0x09,
    MSG_YOURADDR = 0x0A,
    MSG_HOLE_PUNCH = 0x0B,
    MSG_REQUEST_BLOCK = 0x0C,
    MSG_SEND_BLOCK = 0x0D
};

class PacketSerializer {
public:
    static std::vector<uint8_t> createPing(const NodeId& sender) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_PING);
        packet.insert(packet.end(), sender.begin(), sender.end());
        return packet;
    }
    
    static std::vector<uint8_t> createPong(const NodeId& sender) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_PONG);
        packet.insert(packet.end(), sender.begin(), sender.end());
        return packet;
    }
    
    static std::vector<uint8_t> createFindNode(const NodeId& sender, 
                                                const NodeId& target) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_FIND_NODE);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), target.begin(), target.end());
        return packet;
    }
    
    static std::vector<uint8_t> createFoundNodes(const NodeId& sender,
                                                   const std::vector<NodeInfo>& nodes) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_FOUND_NODES);
        packet.insert(packet.end(), sender.begin(), sender.end());
        
        // Number of nodes (2 bytes)
        uint16_t count = static_cast<uint16_t>(nodes.size());
        packet.push_back(count >> 8);
        packet.push_back(count & 0xFF);
        
        for (const auto& node : nodes) {
            packet.insert(packet.end(), node.id.begin(), node.id.end());
            
            // IP address (4 bytes)
            uint32_t ip = node.addr.sin_addr.s_addr;
            packet.push_back((ip >> 24) & 0xFF);
            packet.push_back((ip >> 16) & 0xFF);
            packet.push_back((ip >> 8) & 0xFF);
            packet.push_back(ip & 0xFF);
            
            // Port (2 bytes)
            uint16_t port = ntohs(node.addr.sin_port);
            packet.push_back(port >> 8);
            packet.push_back(port & 0xFF);
        }
        
        return packet;
    }
    
    static std::vector<uint8_t> createFindValue(const NodeId& sender,
                                                  const Sha256Hash& target) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_FIND_VALUE);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), target.begin(), target.end());
        return packet;
    }
    
    static std::vector<uint8_t> createFoundValue(const NodeId& sender,
                                                   const Sha256Hash& hash,
                                                   const std::vector<uint8_t>& data) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_FOUND_VALUE);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), hash.begin(), hash.end());
        
        // Data length (4 bytes)
        uint32_t len = static_cast<uint32_t>(data.size());
        packet.push_back((len >> 24) & 0xFF);
        packet.push_back((len >> 16) & 0xFF);
        packet.push_back((len >> 8) & 0xFF);
        packet.push_back(len & 0xFF);
        
        packet.insert(packet.end(), data.begin(), data.end());
        return packet;
    }
    
    static std::vector<uint8_t> createStore(const NodeId& sender,
                                              const Sha256Hash& hash,
                                              const std::vector<uint8_t>& data) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_STORE);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), hash.begin(), hash.end());
        
        uint32_t len = static_cast<uint32_t>(data.size());
        packet.push_back((len >> 24) & 0xFF);
        packet.push_back((len >> 16) & 0xFF);
        packet.push_back((len >> 8) & 0xFF);
        packet.push_back(len & 0xFF);
        
        packet.insert(packet.end(), data.begin(), data.end());
        return packet;
    }
    
    static std::vector<uint8_t> createStored(const NodeId& sender,
                                               const Sha256Hash& hash) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_STORED);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), hash.begin(), hash.end());
        return packet;
    }
    
    static std::vector<uint8_t> createWhoAmI(const NodeId& sender) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_WHOAMI);
        packet.insert(packet.end(), sender.begin(), sender.end());
        return packet;
    }
    
    static std::vector<uint8_t> createYourAddr(const NodeId& sender,
                                                 const sockaddr_in& addr) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_YOURADDR);
        packet.insert(packet.end(), sender.begin(), sender.end());
        
        uint32_t ip = addr.sin_addr.s_addr;
        packet.push_back((ip >> 24) & 0xFF);
        packet.push_back((ip >> 16) & 0xFF);
        packet.push_back((ip >> 8) & 0xFF);
        packet.push_back(ip & 0xFF);
        
        uint16_t port = ntohs(addr.sin_port);
        packet.push_back(port >> 8);
        packet.push_back(port & 0xFF);
        
        return packet;
    }
    
    static std::vector<uint8_t> createHolePunch(const NodeId& sender,
                                                  const NodeId& targetId,
                                                  const sockaddr_in& targetAddr) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_HOLE_PUNCH);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), targetId.begin(), targetId.end());
        
        uint32_t ip = targetAddr.sin_addr.s_addr;
        packet.push_back((ip >> 24) & 0xFF);
        packet.push_back((ip >> 16) & 0xFF);
        packet.push_back((ip >> 8) & 0xFF);
        packet.push_back(ip & 0xFF);
        
        uint16_t port = ntohs(targetAddr.sin_port);
        packet.push_back(port >> 8);
        packet.push_back(port & 0xFF);
        
        return packet;
    }
    
    static std::vector<uint8_t> createRequestBlock(const NodeId& sender,
                                                     const Sha256Hash& fileHash,
                                                     uint32_t blockIndex) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_REQUEST_BLOCK);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), fileHash.begin(), fileHash.end());
        
        packet.push_back((blockIndex >> 24) & 0xFF);
        packet.push_back((blockIndex >> 16) & 0xFF);
        packet.push_back((blockIndex >> 8) & 0xFF);
        packet.push_back(blockIndex & 0xFF);
        
        return packet;
    }
    
    static std::vector<uint8_t> createSendBlock(const NodeId& sender,
                                                   const Sha256Hash& fileHash,
                                                   uint32_t blockIndex,
                                                   const std::vector<uint8_t>& data) {
        std::vector<uint8_t> packet;
        packet.push_back(MSG_SEND_BLOCK);
        packet.insert(packet.end(), sender.begin(), sender.end());
        packet.insert(packet.end(), fileHash.begin(), fileHash.end());
        
        packet.push_back((blockIndex >> 24) & 0xFF);
        packet.push_back((blockIndex >> 16) & 0xFF);
        packet.push_back((blockIndex >> 8) & 0xFF);
        packet.push_back(blockIndex & 0xFF);
        
        uint32_t len = static_cast<uint32_t>(data.size());
        packet.push_back((len >> 24) & 0xFF);
        packet.push_back((len >> 16) & 0xFF);
        packet.push_back((len >> 8) & 0xFF);
        packet.push_back(len & 0xFF);
        
        packet.insert(packet.end(), data.begin(), data.end());
        return packet;
    }
    
    static bool parse(const uint8_t* data, size_t len,
                      MessageType& type,
                      NodeId& sender,
                      std::vector<uint8_t>& payload) {
        if (len < 33) return false; // Minimum: type(1) + nodeid(32)
        
        type = static_cast<MessageType>(data[0]);
        std::copy(data + 1, data + 33, sender.begin());
        
        if (len > 33) {
            payload.assign(data + 33, data + len);
        }
        
        return true;
    }
};

} // namespace p2p
