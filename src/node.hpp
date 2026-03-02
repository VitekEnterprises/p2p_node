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

#ifdef _WIN32
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace p2p {

static std::string getLocalMacAddress() {
#ifdef _WIN32
    IP_ADAPTER_INFO AdapterInfo[16];
    DWORD bufLen = sizeof(AdapterInfo);
    if (GetAdaptersInfo(AdapterInfo, &bufLen) == NO_ERROR) {
        PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
        char macAddr[18];
        sprintf_s(macAddr, sizeof(macAddr), "%02X:%02X:%02X:%02X:%02X:%02X",
                pAdapterInfo->Address[0], pAdapterInfo->Address[1],
                pAdapterInfo->Address[2], pAdapterInfo->Address[3],
                pAdapterInfo->Address[4], pAdapterInfo->Address[5]);
        return std::string(macAddr);
    }
    return std::string();
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return std::string();
    std::string result;
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) continue;
        struct ifreq ifr;
        strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ-1);
        if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
            unsigned char *mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
            char buf[18];
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            result = buf;
            close(fd);
            break;
        }
        close(fd);
    }
    freeifaddrs(ifaddr);
    return result;
#endif
}

class P2PNode {
public:
    P2PNode(uint16_t port, const std::string& storagePath)
        : port_(port),
          socket_(port),
          nodeId_(loadOrCreateNodeId()),
          routingTable_(nodeId_),
          dht_(socket_, routingTable_, nodeId_),
          holePuncher_(socket_, nodeId_),
          blockStore_(storagePath) {
        macAddr_ = getLocalMacAddress();
        // compute hardware id as hash of mac + node id
        std::string macPlusId;
        macPlusId.reserve(macAddr_.size() + 64);
        macPlusId += macAddr_;
        macPlusId += SHA256::toHex(nodeId_);
        auto hw = SHA256::hash(reinterpret_cast<const uint8_t*>(macPlusId.data()), macPlusId.size());
        hwid_ = SHA256::toHex(hw);
    }
    
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
            uint16_t port = static_cast<uint16_t>(std::stoi(node.substr(colonPos + 1)));
            
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
    
    bool shareFile(const std::string& filepath, bool isPublic = false) {
        FileMetadata metadata;
        if (!blockStore_.storeFile(filepath, metadata)) {
            return false;
        }
        // inform user of the hash needed for download
        std::string hashStr = SHA256::toHex(metadata.fileHash);
        Logger::info("Shared file hash: " + hashStr);

        // record in our own table immediately
        // Note: This method needs to be added to DHTProtocol
        // dht_.recordLocalHash(metadata.fileHash);

        // read file contents into memory and store in DHT
        std::ifstream in(filepath, std::ios::binary);
        if (in) {
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            dht_.store(metadata.fileHash, metadata.filename, isPublic, data);
        }

        // track that we shared this file
        sharedFiles_[metadata.filename] = metadata.fileHash;

        // if public announce to peers
        if (isPublic) {
            // send announcement to all nodes in routing table
            for (const auto& n : routingTable_.getAllNodes()) {
                auto packet = PacketSerializer::createShareAnnounce(nodeId_, metadata.filename, metadata.fileHash);
                socket_.sendTo(packet.data(), packet.size(), n.addr);
            }
        }

        return true;
    }
    
    void downloadFile(const Sha256Hash& fileHash, const std::string& savePath) {
        // Note: getOwners method needs to be added to DHTProtocol
        // auto owners = dht_.getOwners(fileHash);
        // if (owners.empty()) {
        //     Logger::warn("No active peers with that hash");
        //     return;
        // }
        // at least one owner known; proceed using DHT mechanism
        Logger::info("Attempting download via DHT");
        
        dht_.findValue(fileHash, [this, fileHash, savePath](const std::vector<uint8_t>& data, const std::string& filename) {
            if (data.empty()) {
                Logger::warn("File not found in network");
                return;
            }
            
            std::string outFile = savePath + "/" + filename;
            std::ofstream out(outFile, std::ios::binary);
            if (out) {
                out.write(reinterpret_cast<const char*>(data.data()), data.size());
                Logger::info("File downloaded to: " + outFile);
                // track that we downloaded this file
                downloadedFiles_[filename] = fileHash;
                // dht_.recordLocalHash(fileHash); // Commented out until method is added
                // dht_.recordDownload(fileHash); // Commented out until method is added
            } else {
                Logger::error("Failed to open output file: " + outFile);
            }
        });
    }
    
    NodeId getNodeId() const { return nodeId_; }
    RoutingTable& getRoutingTable() { return routingTable_; }

    // accessors for MAC/hardware
    std::string getMacAddress() const { return macAddr_; }
    std::string getHardwareId() const { return hwid_; }

    // chat forwarding
    void sendChat(const std::string& msg) {
        dht_.sendChat(msg);
    }

    // hash table synchronization
    void syncHashes() {
        auto peers = routingTable_.getAllNodes();
        for (const auto& n : peers) {
            dht_.requestHashList(n.addr);
        }
    }

    // user registry synchronization
    void syncUserRegistry() {
        auto peers = routingTable_.getAllNodes();
        for (const auto& n : peers) {
            dht_.requestUserRegistry(n.addr);
        }
    }

    // find which online peers have a specific hash
    std::vector<std::string> findOnlineOwners(const Sha256Hash& fileHash) const {
        // Note: getOwners method needs to be added to DHTProtocol
        // auto owners = dht_.getOwners(fileHash);
        std::vector<std::string> result;
        // if (owners.empty()) return result;
        
        auto rtable = routingTable_.getAllNodes();
        // for (const auto& owner : owners) {
        //     for (const auto& node : rtable) {
        //         if (node.addr.sin_addr.s_addr == owner.sin_addr.s_addr && 
        //             node.addr.sin_port == owner.sin_port) {
        //             char ipStr[INET_ADDRSTRLEN];
        //             inet_ntop(AF_INET, &owner.sin_addr, ipStr, INET_ADDRSTRLEN);
        //             result.push_back(std::string(ipStr) + ":" + 
        //                            std::to_string(ntohs(owner.sin_port)));
        //             break;
        //         }
        //     }
        // }
        return result;
    }

    // user management
    std::string getUserNick(const std::string& hwid) const {
        return dht_.getUserNick(hwid);
    }

    void announceMyself(const std::string& hwid, const std::string& nick) {
        dht_.announceUser(hwid, nick);
    }

    std::unordered_map<std::string, std::string> getAllUsers() const {
        std::unordered_map<std::string, std::string> result;
        for (const auto& entry : dht_.getUserRegistry()) {
            result[entry.first] = entry.second.nick;
        }
        return result;
    }

    void loadUserRegistry(const std::unordered_map<std::string, std::string>& users) {
        std::unordered_map<std::string, DHTProtocol::UserInfo> reg;
        for (const auto& p : users) {
            DHTProtocol::UserInfo info;
            info.nick = p.second;
            info.lastSeen = std::chrono::system_clock::now();
            reg[p.first] = info;
        }
        dht_.setUserRegistry(reg);
    }

    struct HashEntry {
        std::string hashStr;
        int downloadCount;
        std::string filename;
        std::string lastSeen;
    };

    std::vector<HashEntry> getHashStats() const {
        auto stats = dht_.getHashStats();
        std::vector<HashEntry> out;
        for (const auto& s : stats) {
            HashEntry h;
            h.hashStr = s.hashStr;
            h.downloadCount = s.downloadCount;
            h.filename = s.filename;
            h.lastSeen = s.lastSeen;
            out.push_back(h);
        }
        return out;
    }

    // get list of downloaded files (filename -> hash)
    std::vector<std::pair<std::string, std::string>> getDownloadedFiles() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& p : downloadedFiles_) {
            out.push_back({p.first, SHA256::toHex(p.second)});
        }
        return out;
    }

    // get list of shared files (filename -> hash)
    std::vector<std::pair<std::string, std::string>> getSharedFiles() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& p : sharedFiles_) {
            out.push_back({p.first, SHA256::toHex(p.second)});
        }
        return out;
    }

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

    std::string macAddr_;
    std::string hwid_;
    
    // track downloaded and shared files
    std::unordered_map<std::string, Sha256Hash> downloadedFiles_; // filename -> hash
    std::unordered_map<std::string, Sha256Hash> sharedFiles_;     // filename -> hash
    
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