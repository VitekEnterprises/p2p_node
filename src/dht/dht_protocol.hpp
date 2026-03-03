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
    // small in-memory store for shared values (files) to support download
    struct Sha256HashHash {
        size_t operator()(const Sha256Hash& h) const noexcept {
            // combine 4 uint64_t chunks
            const uint64_t* p = reinterpret_cast<const uint64_t*>(h.data());
            return p[0] ^ p[1] ^ p[2] ^ p[3];
        }
    };
    struct Sha256HashEq {
        bool operator()(const Sha256Hash& a, const Sha256Hash& b) const noexcept {
            return a == b;
        }
    };

    // user registry info is public so callers can inspect nick/lastSeen
    struct UserInfo {
        std::string nick;
        std::chrono::system_clock::time_point lastSeen;
    };

private:
    struct StoredValue {
        std::vector<uint8_t> data;
        std::string filename;
        bool isPublic;
    };
    std::unordered_map<Sha256Hash, StoredValue, Sha256HashHash, Sha256HashEq> valueStore_; // hash->value with metadata

    // hash table containing known shares from peers (including ourselves)
    struct HashInfo {
        std::vector<sockaddr_in> owners; // nodes that advertised/have the file
        std::chrono::system_clock::time_point lastSeen;
        int downloadCount = 0; // how many times this hash was downloaded
        std::string filename; // name of the file
    };
    std::unordered_map<Sha256Hash, HashInfo, Sha256HashHash, Sha256HashEq> hashTable_;

    // user registry: hardware_id (mac+node_id hash) -> nickname
    std::unordered_map<std::string, UserInfo> userRegistry_; // hwid -> info
    
    // mapping: NodeID -> hardware ID (for proper user identification in chat)
    std::unordered_map<std::string, std::string> nodeIdToHwid_; // SHA256::toHex(NodeID) -> hwid

public:
    using FoundNodeCallback = std::function<void(const std::vector<NodeInfo>&)>;
    using FoundValueCallback = std::function<void(const std::vector<uint8_t>&, const std::string& filename)>;
    
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
            case MSG_SHARE_ANNOUNCE:
                handleShareAnnounce(senderId, sender, payload);
                break;
            case MSG_HASH_LIST_REQUEST:
                handleHashListRequest(senderId, sender);
                break;
            case MSG_HASH_LIST:
                handleHashList(senderId, sender, payload);
                break;
            case MSG_USER_REGISTRY_REQUEST:
                handleUserRegistryRequest(senderId, sender);
                break;
            case MSG_USER_REGISTRY:
                handleUserRegistry(senderId, sender, payload);
                break;
            case MSG_USER_ANNOUNCE:
                handleUserAnnounce(senderId, sender, payload);
                break;
            case MSG_NICK:
                handleNickAnnounce(senderId, sender, payload);
                break;
            case MSG_CHAT:
                handleChat(senderId, sender, payload);
                break;
            case MSG_REQUEST_BLOCK:
            case MSG_SEND_BLOCK:
            case MSG_WEBSITE_LIST:
            case MSG_WEBSITE_REQUEST:
            case MSG_WEBSITE_METADATA:
                // handled at P2PNode level (block transfer path)
                break;
            default:
                Logger::debug("Unknown message type: " + std::to_string(type));
        }
    }
    
    void ping(const sockaddr_in& addr) {
        auto packet = PacketSerializer::createPing(selfId_);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }

    // send our known hash list to a peer
    void sendHashList(const sockaddr_in& dest) {
        std::vector<Sha256Hash> hashes;
        for (const auto& entry : hashTable_) {
            hashes.push_back(entry.first);
        }
        auto packet = PacketSerializer::createHashList(selfId_, hashes);
        socket_.sendTo(packet.data(), packet.size(), dest);
    }

    // request hash list from a peer
    void requestHashList(const sockaddr_in& dest) {
        auto packet = PacketSerializer::createHashListRequest(selfId_);
        socket_.sendTo(packet.data(), packet.size(), dest);
    }

    void sendUserRegistry(const sockaddr_in& dest) {
        std::vector<std::pair<std::string, std::string>> users;
        for (const auto& entry : userRegistry_) {
            users.push_back({entry.first, entry.second.nick});
        }
        auto packet = PacketSerializer::createUserRegistry(selfId_, users);
        socket_.sendTo(packet.data(), packet.size(), dest);
    }

    void announceUser(const std::string& hwid, const std::string& nick) {
        for (const auto& node : routingTable_.getAllNodes()) {
            auto packet = PacketSerializer::createUserAnnounce(selfId_, hwid, nick);
            socket_.sendTo(packet.data(), packet.size(), node.addr);
        }
        registerUser(hwid, nick);
    }

    void requestUserRegistry(const sockaddr_in& dest) {
        auto packet = PacketSerializer::createUserRegistryRequest(selfId_);
        socket_.sendTo(packet.data(), packet.size(), dest);
    }

    void sendChat(const std::string& msg) {
        for (const auto& node : routingTable_.getAllNodes()) {
            auto packet = PacketSerializer::createChat(selfId_, msg);
            socket_.sendTo(packet.data(), packet.size(), node.addr);
        }
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
    
    void store(const Sha256Hash& hash, const std::string& filename, bool isPublic, const std::vector<uint8_t>& data) {
        // keep local copy so we can answer FIND_VALUE
        valueStore_[hash] = {data, filename, isPublic};

        auto closest = routingTable_.findClosest(hash, 8);
        for (const auto& node : closest) {
            auto packet = PacketSerializer::createStore(selfId_, hash, filename, isPublic, data);
            socket_.sendTo(packet.data(), packet.size(), node.addr);
        }
    }
    
    void refreshBuckets() {
        auto allNodes = routingTable_.getAllNodes();
        for (const auto& node : allNodes) {
            ping(node.addr);
        }
    }

    // return detailed info for each hash including download count
    struct HashStat {
        std::string hashStr;
        int downloadCount;
        std::string filename;
        std::string lastSeen;
    };

    std::vector<HashStat> getHashStats() const {
        std::vector<HashStat> out;
        for (const auto& entry : hashTable_) {
            HashStat h;
            h.hashStr = SHA256::toHex(entry.first);
            h.downloadCount = entry.second.downloadCount;
            h.filename = entry.second.filename;
            auto t = entry.second.lastSeen;
            if (t != std::chrono::system_clock::time_point{}) {
                auto tt = std::chrono::system_clock::to_time_t(t);
                h.lastSeen = std::ctime(&tt);
                if (!h.lastSeen.empty() && h.lastSeen.back() == '\n')
                    h.lastSeen.pop_back();
            }
            out.push_back(h);
        }
        return out;
    }

    // increment download count for a hash
    void recordDownload(const Sha256Hash& hash) {
        auto& info = hashTable_[hash];
        info.downloadCount++;
        info.lastSeen = std::chrono::system_clock::now();
    }

    // record that we have a local copy of a hash (mark last seen)
    void recordLocalHash(const Sha256Hash& hash) {
        auto& info = hashTable_[hash];
        info.lastSeen = std::chrono::system_clock::now();
    }

    // return owners for a given hash
    std::vector<sockaddr_in> getOwners(const Sha256Hash& hash) const {
        auto it = hashTable_.find(hash);
        if (it == hashTable_.end()) return {};
        return it->second.owners;
    }

    // register/update a user in the registry
    void registerUser(const std::string& hwid, const std::string& nick) {
        userRegistry_[hwid] = {nick, std::chrono::system_clock::now()};
    }

    // get user nick if registered
    std::string getUserNick(const std::string& hwid) const {
        auto it = userRegistry_.find(hwid);
        if (it != userRegistry_.end()) {
            return it->second.nick;
        }
        return "";
    }

    // get all users (for persistence)
    const std::unordered_map<std::string, UserInfo>& getUserRegistry() const {
        return userRegistry_;
    }

    void setUserRegistry(const std::unordered_map<std::string, UserInfo>& reg) {
        userRegistry_ = reg;
    }

    // return vector of all known hashes
    std::vector<Sha256Hash> getKnownHashes() const {
        std::vector<Sha256Hash> out;
        out.reserve(hashTable_.size());
        for (const auto& entry : hashTable_) out.push_back(entry.first);
        return out;
    }

    std::chrono::system_clock::time_point getLastSeen(const Sha256Hash& hash) const {
        auto it = hashTable_.find(hash);
        if (it != hashTable_.end()) {
            return it->second.lastSeen;
        }
        return std::chrono::system_clock::time_point{};
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
        // ask this peer for its list of hashes to keep our table up to date
        requestHashList(addr);
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
        
                // if we have the value locally, reply with it
        auto it = valueStore_.find(hash);
        if (it != valueStore_.end()) {
            // only send it if public or requester already has hash (we don't track requesters)
            // privacy is enforced by not announcing the hash publicly; anyone who asks by hash gets data
            auto packet = PacketSerializer::createFoundValue(selfId_, hash, it->second.filename, it->second.data);
            socket_.sendTo(packet.data(), packet.size(), addr);
            return;
        }

        // otherwise just return closest nodes
        auto closest = routingTable_.findClosest(hash, 8);
        auto packet = PacketSerializer::createFoundNodes(selfId_, closest);
        socket_.sendTo(packet.data(), packet.size(), addr);
    }
    
    void handleFoundValue(const NodeId& sender, const sockaddr_in& addr,
                          const std::vector<uint8_t>& payload) {
        if (payload.size() < 32 + 2) return; // hash + filename length
        Sha256Hash hash;
        std::copy(payload.begin(), payload.begin() + 32, hash.begin());
        
        size_t pos = 32;
        uint16_t nameLen = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + nameLen + 4) return;
        std::string filename(reinterpret_cast<const char*>(&payload[pos]), nameLen);
        pos += nameLen;

        uint32_t len = (payload[pos] << 24) | (payload[pos+1] << 16) |
                       (payload[pos+2] << 8) | payload[pos+3];
        pos += 4;
        if (payload.size() < pos + len) return;
        std::vector<uint8_t> data(payload.begin() + pos, payload.begin() + pos + len);
        
        // find pending query and invoke callback
        uint64_t toErase = 0;
        for (auto it = pendingQueries_.begin(); it != pendingQueries_.end(); ++it) {
            if (it->second.type == PendingQuery::FIND_VALUE && it->second.target == hash) {
                if (it->second.valueCallback) {
                    it->second.valueCallback(data, filename);
                }
                toErase = it->first;
                break;
            }
        }
        if (toErase != 0) {
            pendingQueries_.erase(toErase);
        }
    }
    
    void handleStore(const NodeId& sender, const sockaddr_in& addr,
                     const std::vector<uint8_t>& payload) {
        // parse hash, filename, privacy and data
        if (payload.size() < 32 + 2 + 1 + 4) return;
        size_t pos = 0;
        Sha256Hash hash;
        std::copy(payload.begin(), payload.begin() + 32, hash.begin());
        pos += 32;

        uint16_t nameLen = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + nameLen + 1 + 4) return;
        std::string filename(reinterpret_cast<const char*>(&payload[pos]), nameLen);
        pos += nameLen;

        bool isPublic = payload[pos] != 0;
        pos += 1;

        uint32_t len = (payload[pos] << 24) | (payload[pos+1] << 16) |
                       (payload[pos+2] << 8) | payload[pos+3];
        pos += 4;
        if (payload.size() < pos + len) return;
        std::vector<uint8_t> data(payload.begin() + pos, payload.begin() + pos + len);

        // store locally
        valueStore_[hash] = {data, filename, isPublic};
        
        // also record in hash table for tracking
        auto& hashInfo = hashTable_[hash];
        hashInfo.filename = filename;
        if (hashInfo.lastSeen == std::chrono::system_clock::time_point{}) {
            hashInfo.lastSeen = std::chrono::system_clock::now();
        }

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
    
    void handleShareAnnounce(const NodeId& sender, const sockaddr_in& addr,
                               const std::vector<uint8_t>& payload) {
        // parse filename length, name, hash
        if (payload.size() < 2 + 32) return;
        size_t pos = 0;
        uint16_t nameLen = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + nameLen + 32) return;
        std::string filename(reinterpret_cast<const char*>(&payload[pos]), nameLen);
        pos += nameLen;
        Sha256Hash hash;
        std::copy(payload.begin() + pos, payload.begin() + pos + 32, hash.begin());
        pos += 32;
        // update hash table without logging to console
        auto& info = hashTable_[hash];
        // ignore obviously invalid addresses (loopback/any); they are not useful
        if (addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) &&
            addr.sin_addr.s_addr != htonl(INADDR_ANY)) {
            // add owner if not already present
            bool found = false;
            for (auto &a : info.owners) {
                if (a.sin_addr.s_addr == addr.sin_addr.s_addr && a.sin_port == addr.sin_port) {
                    found = true;
                    break;
                }
            }
            if (!found) info.owners.push_back(addr);
        }
        info.lastSeen = std::chrono::system_clock::now();
    }

    void handleHashListRequest(const NodeId& sender, const sockaddr_in& addr) {
        // peer is asking for our known hashes
        sendHashList(addr);
    }

    void handleHashList(const NodeId& sender, const sockaddr_in& addr,
                        const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        uint16_t count = (payload[0] << 8) | payload[1];
        size_t pos = 2;
        for (uint16_t i = 0; i < count; ++i) {
            if (payload.size() < pos + 32) break;
            Sha256Hash hash;
            std::copy(payload.begin() + pos, payload.begin() + pos + 32, hash.begin());
            pos += 32;
            auto& info = hashTable_[hash];
            // only record address if it isn't loopback/any
            if (addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) &&
                addr.sin_addr.s_addr != htonl(INADDR_ANY)) {
                bool found = false;
                for (auto &a : info.owners) {
                    if (a.sin_addr.s_addr == addr.sin_addr.s_addr && a.sin_port == addr.sin_port) {
                        found = true;
                        break;
                    }
                }
                if (!found) info.owners.push_back(addr);
            }
            info.lastSeen = std::chrono::system_clock::now();
        }
    }

    void handleUserRegistryRequest(const NodeId& sender, const sockaddr_in& addr) {
        sendUserRegistry(addr);
    }

    void handleUserRegistry(const NodeId& sender, const sockaddr_in& addr,
                            const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        uint16_t count = (payload[0] << 8) | payload[1];
        size_t pos = 2;
        for (uint16_t i = 0; i < count; ++i) {
            if (payload.size() < pos + 2) break;
            uint16_t hlen = (payload[pos] << 8) | payload[pos+1];
            pos += 2;
            if (payload.size() < pos + hlen + 2) break;
            std::string hwid(reinterpret_cast<const char*>(&payload[pos]), hlen);
            pos += hlen;
            uint16_t nlen = (payload[pos] << 8) | payload[pos+1];
            pos += 2;
            if (payload.size() < pos + nlen) break;
            std::string nick(reinterpret_cast<const char*>(&payload[pos]), nlen);
            pos += nlen;
            registerUser(hwid, nick);
        }
    }

    void handleUserAnnounce(const NodeId& sender, const sockaddr_in& addr,
                            const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        size_t pos = 0;
        uint16_t hlen = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + hlen + 2) return;
        std::string hwid(reinterpret_cast<const char*>(&payload[pos]), hlen);
        pos += hlen;
        uint16_t nlen = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + nlen) return;
        std::string nick(reinterpret_cast<const char*>(&payload[pos]), nlen);
        pos += nlen;
        
        // record mapping from NodeID to hwid
        nodeIdToHwid_[SHA256::toHex(sender)] = hwid;
        
        registerUser(hwid, nick);
    }

    void handleNickAnnounce(const NodeId& sender, const sockaddr_in& addr,
                             const std::vector<uint8_t>& payload) {
        size_t pos = 0;
        if (payload.size() < pos + 2) return;
        uint16_t len = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + len + 2) return;
        std::string nick(reinterpret_cast<const char*>(&payload[pos]), len);
        pos += len;
        uint16_t mlen = (payload[pos] << 8) | payload[pos+1];
        pos += 2;
        if (payload.size() < pos + mlen) return;
        std::string mac(reinterpret_cast<const char*>(&payload[pos]), mlen);
        
        std::string key = SHA256::toHex(sender);
        auto itexisting = userRegistry_.find(key);
        if (itexisting != userRegistry_.end()) {
            // update last seen
            itexisting->second.lastSeen = std::chrono::system_clock::now();
        } else {
            // new user
            userRegistry_[key] = {nick, std::chrono::system_clock::now()};
        }
        Logger::info("Node " + nick + " (" + key + ") announced");
    }

    void handleChat(const NodeId& sender, const sockaddr_in& addr,
                    const std::vector<uint8_t>& payload) {
        if (payload.size() < 2) return;
        uint16_t len = (payload[0] << 8) | payload[1];
        if (payload.size() < 2 + len) return;
        std::string msg(reinterpret_cast<const char*>(&payload[2]), len);
        
        std::string senderNodeIdHex = SHA256::toHex(sender);
        std::string hwid;
        std::string nick = senderNodeIdHex.substr(0, 8); // fallback
        
        // find hwid from NodeID mapping
        auto it_hwid = nodeIdToHwid_.find(senderNodeIdHex);
        if (it_hwid != nodeIdToHwid_.end()) {
            hwid = it_hwid->second;
            // now find nick from hwid
            auto it_nick = userRegistry_.find(hwid);
            if (it_nick != userRegistry_.end()) {
                nick = it_nick->second.nick;
            }
        }
        
        Logger::info("[CHAT] " + nick + ": " + msg);
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
