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
#include <memory>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <iomanip>

namespace p2p { class P2PNode; }
extern std::unique_ptr<p2p::P2PNode> g_node;

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

static std::string runCommandCapture(const char* cmd) {
#ifdef _WIN32
    FILE* pipe = _popen(cmd, "r");
#else
    FILE* pipe = popen(cmd, "r");
#endif
    if (!pipe) return std::string();
    std::string result;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \n\r\t");
    if (start == std::string::npos) return std::string();
    auto end = s.find_last_not_of(" \n\r\t");
    return s.substr(start, end - start + 1);
}

static std::string computeStableHardwareId() {
#ifdef _WIN32
    // Query CPU processor id
    std::string cpuOut = runCommandCapture("wmic cpu get processorid");
    std::istringstream scpu(cpuOut);
    std::string line;
    std::string cpuId;
    while (std::getline(scpu, line)) {
        line = trim(line);
        if (line.empty()) continue;
        // skip header if present
        if (line == "ProcessorId" || line == "ProcessorId\r") continue;
        cpuId = line;
        break;
    }

    // Query system UUID
    std::string uuidOut = runCommandCapture("wmic csproduct get uuid");
    std::istringstream su(uuidOut);
    std::string uuid;
    while (std::getline(su, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line == "UUID" || line == "UUID\r") continue;
        uuid = line;
        break;
    }

    // Get MAC address (fallback to getLocalMacAddress)
    std::string mac = getLocalMacAddress();
    if (mac.empty()) {
        // try getmac
        std::string macOut = runCommandCapture("getmac /NH /FO CSV");
        // take first token that looks like a mac
        std::istringstream sm(macOut);
        while (std::getline(sm, line)) {
            line = trim(line);
            if (line.empty()) continue;
            // simple heuristic: contains '-'
            if (line.find('-') != std::string::npos || line.find(':') != std::string::npos) {
                // strip quotes and CSV fields
                size_t p = line.find('"');
                if (p != std::string::npos) line.erase(p, 1);
                p = line.find('"');
                if (p != std::string::npos) line.erase(p, 1);
                mac = line;
                break;
            }
        }
    }

    std::string combined = cpuId + "|" + uuid + "|" + mac;
    // if all empty, fall back to mac+nodeid behavior (handled by caller if needed)
    if (combined.find_first_not_of("| ") == std::string::npos) return std::string();

    auto hw = SHA256::hash(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
    return SHA256::toHex(hw);
#else
    // Non-Windows: fallback to MAC + stable system identifiers if available
    std::string mac = getLocalMacAddress();
    if (mac.empty()) mac = "UNKNOWN_MAC";
    auto hw = SHA256::hash(reinterpret_cast<const uint8_t*>(mac.data()), mac.size());
    return SHA256::toHex(hw);
#endif
}

class P2PNode {
public:
    P2PNode(uint16_t port, const std::string& storagePath)
        : port_(port),
          storagePath_(storagePath),
          socket_(port),
          nodeId_(loadOrCreateNodeId()),
          routingTable_(nodeId_),
          dht_(socket_, routingTable_, nodeId_),
          holePuncher_(socket_, nodeId_),
          blockStore_(storagePath) {
        macAddr_ = getLocalMacAddress();
        // compute stable hardware id from system identifiers
        hwid_ = computeStableHardwareId();
        if (hwid_.empty()) {
            // fallback: hash of mac + node id (keeps previous behavior if system ids unavailable)
            std::string macPlusId;
            macPlusId.reserve(macAddr_.size() + 64);
            macPlusId += macAddr_;
            macPlusId += SHA256::toHex(nodeId_);
            auto hw = SHA256::hash(reinterpret_cast<const uint8_t*>(macPlusId.data()), macPlusId.size());
            hwid_ = SHA256::toHex(hw);
        }
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
        
        // load any persisted data
        loadFileList("downloaded.dat", downloadedFiles_);
        loadFileList("shared.dat", sharedFiles_);
        loadHashesFromDisk();
        loadUsersFromDisk();
        
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

        // build metadata blob (filesize + totalBlocks + blockSize + block hashes)
        std::vector<uint8_t> metaBytes;
        // filesize (8 bytes)
        uint64_t fs = metadata.filesize;
        for (int i = 7; i >= 0; --i) metaBytes.push_back((fs >> (i*8)) & 0xFF);
        // totalBlocks (4 bytes)
        uint32_t tb = metadata.totalBlocks;
        metaBytes.push_back((tb >> 24) & 0xFF);
        metaBytes.push_back((tb >> 16) & 0xFF);
        metaBytes.push_back((tb >> 8) & 0xFF);
        metaBytes.push_back(tb & 0xFF);
        // blockSize (4 bytes)
        uint32_t bs = metadata.blockSize;
        metaBytes.push_back((bs >> 24) & 0xFF);
        metaBytes.push_back((bs >> 16) & 0xFF);
        metaBytes.push_back((bs >> 8) & 0xFF);
        metaBytes.push_back(bs & 0xFF);
        // block hashes
        for (const auto &h : metadata.blockHashes) {
            metaBytes.insert(metaBytes.end(), h.begin(), h.end());
        }
        // store metadata in DHT rather than full file data
        dht_.store(metadata.fileHash, metadata.filename, isPublic, metaBytes);

        // track that we shared this file and persist
        sharedFiles_[metadata.filename] = metadata.fileHash;
        saveFileList("shared.dat", sharedFiles_);

        // record that we have this hash locally (owner) so it appears in table
        dht_.recordLocalHash(metadata.fileHash);

        // if public we should remember the hash locally so it ends up in hashes.dat
        if (isPublic) {
            if (knownHashes_.insert(metadata.fileHash).second) {
                appendHashToDisk(metadata.fileHash);
            }
        }
        std::string nick = getUserNick(SHA256::toHex(nodeId_));

        // if public announce to peers
        if (isPublic) {
            std::string hashStr = SHA256::toHex(metadata.fileHash);

                // najít nick podle nodeId → hwid → nick
                std::string senderNodeIdHex = SHA256::toHex(nodeId_);
                std::string hwid;

                auto it_hwid = nodeIdToHwid_.find(senderNodeIdHex);
                if (it_hwid != nodeIdToHwid_.end()) {
                    hwid = it_hwid->second;

                    auto it_nick = userRegistry_.find(hwid);
                    if (it_nick != userRegistry_.end()) {
                        nick = it_nick->second.nick;
                    }
                }

                // poslat chat zprávu všem
                ::g_node->sendChat(nick + " shared file: " + metadata.filename + " (hash: " + hashStr + ")");

                // původní announce packet
                for (const auto& n : routingTable_.getAllNodes()) {
                    auto packet = PacketSerializer::createShareAnnounce(nodeId_, metadata.filename, metadata.fileHash);
                    socket_.sendTo(packet.data(), packet.size(), n.addr);
                }
            }
        return true;
    }
    
    // parse metadata blob into FileMetadata; supports new and legacy format
    bool parseMetadata(const std::vector<uint8_t>& data, FileMetadata &m) {
        if (data.size() < 12) return false;
        size_t pos = 0;
        m.filesize = 0;
        for (int i = 0; i < 8; ++i) {
            m.filesize = (m.filesize << 8) | data[pos++];
        }
        m.totalBlocks = 0;
        for (int i = 0; i < 4; ++i) {
            m.totalBlocks = (m.totalBlocks << 8) | data[pos++];
        }

        // new format contains blockSize after totalBlocks
        size_t expectedNew = 8 + 4 + 4 + static_cast<size_t>(m.totalBlocks) * 32;
        size_t expectedLegacy = 8 + 4 + static_cast<size_t>(m.totalBlocks) * 32;
        if (data.size() == expectedNew) {
            m.blockSize = 0;
            for (int i = 0; i < 4; ++i) {
                m.blockSize = (m.blockSize << 8) | data[pos++];
            }
        } else if (data.size() == expectedLegacy) {
            m.blockSize = static_cast<uint32_t>(BlockStore::MIN_BLOCK_SIZE);
        } else {
            return false;
        }

        m.blockHashes.clear();
        for (uint32_t i = 0; i < m.totalBlocks; ++i) {
            Sha256Hash h;
            std::copy(data.begin() + pos, data.begin() + pos + 32, h.begin());
            pos += 32;
            m.blockHashes.push_back(h);
        }
        return true;
    }

    // perform block-based download using owners list and metadata
    void downloadLarge(const Sha256Hash& fileHash,
                       const std::string& savePath,
                       const std::string& filename,
                       const FileMetadata& meta) {
        auto owners = dht_.getOwners(fileHash);
        // filter out loopback/invalid entries; also ignore our own public address if known
        sockaddr_in selfAddr = holePuncher_.getPublicAddress();
        std::vector<sockaddr_in> extOwners;
        for (const auto &o : owners) {
            if (isLocalAddress(o)) {
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &o.sin_addr, buf, sizeof(buf));
                Logger::debug(std::string("Skipping local owner address: ") + buf + ":" + std::to_string(ntohs(o.sin_port)));
                continue;
            }
            // also skip if it matches our public address (hole puncher) to avoid selecting ourselves by WAN IP
            sockaddr_in selfAddr = holePuncher_.getPublicAddress();
            if (selfAddr.sin_port != 0 && o.sin_addr.s_addr == selfAddr.sin_addr.s_addr && o.sin_port == selfAddr.sin_port) {
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &o.sin_addr, buf, sizeof(buf));
                Logger::debug(std::string("Skipping owner matching public address: ") + buf + ":" + std::to_string(ntohs(o.sin_port)));
                continue;
            }
            extOwners.push_back(o);
        }
        if (extOwners.empty()) {
            // if we have the data locally, just write it out
            std::string outFile = savePath + "/" + filename;
            if (blockStore_.loadFile(fileHash, outFile)) {
                Logger::info("Large file already local, copied to: " + outFile);
                downloadedFiles_[filename] = fileHash;
                saveFileList("downloaded.dat", downloadedFiles_);
                dht_.recordLocalHash(fileHash);
            } else {
                Logger::warn("No external owners for large file and local copy unavailable");
            }
            return;
        }
        // pick first external owner for simplicity
        auto target = extOwners[0];
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &target.sin_addr, ipStr, INET_ADDRSTRLEN);
        Logger::info("Downloading " + filename + " (" + std::to_string(meta.totalBlocks) + " blocks, blockSize=" +
                 std::to_string(meta.blockSize) + "B) from " + std::string(ipStr) + ":" + std::to_string(ntohs(target.sin_port)));
        
        // request all blocks sequentially with retry/timeout
        for (uint32_t idx = 0; idx < meta.totalBlocks; ++idx) {
            Sha256Hash bh = meta.blockHashes[idx];
            bool received = blockStore_.hasBlock(bh);
            int tries = 0;
            while (!received && tries < 60) { // ~30s max per block
                auto packet = PacketSerializer::createRequestBlock(nodeId_, fileHash, idx);
                socket_.sendTo(packet.data(), packet.size(), target);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                received = blockStore_.hasBlock(bh);
                tries++;
            }
            if (!received) {
                Logger::error("Timeout waiting for block " + std::to_string(idx + 1) +
                              "/" + std::to_string(meta.totalBlocks));
                std::cout << std::endl;
                return;
            }

            double percent = (static_cast<double>(idx + 1) * 100.0) / static_cast<double>(meta.totalBlocks);
            std::cout << "\r[INFO] Download progress: " << (idx + 1) << "/" << meta.totalBlocks
                      << " (" << std::fixed << std::setprecision(1) << percent << "%)" << std::flush;
        }
        std::cout << std::endl;
        // reconstruct file from downloaded block hashes in metadata
        std::filesystem::create_directories(savePath);
        std::string outFile = savePath + "/" + filename;
        std::ofstream out(outFile, std::ios::binary);
        if (!out) {
            Logger::error("Failed to open output file: " + outFile);
            return;
        }
        for (uint32_t idx = 0; idx < meta.totalBlocks; ++idx) {
            std::vector<uint8_t> blockData;
            if (!blockStore_.loadBlock(meta.blockHashes[idx], blockData)) {
                Logger::error("Missing downloaded block during reconstruction: " + std::to_string(idx + 1));
                return;
            }
            out.write(reinterpret_cast<const char*>(blockData.data()), blockData.size());
            if (!out.good()) {
                Logger::error("Failed writing output file: " + outFile);
                return;
            }
        }
        Logger::info("Large file downloaded to: " + outFile);
        downloadedFiles_[filename] = fileHash;
        saveFileList("downloaded.dat", downloadedFiles_);
        dht_.recordLocalHash(fileHash);
    }

    void downloadFile(const Sha256Hash& fileHash, const std::string& savePath) {
        Logger::info("Attempting download via DHT");
        dht_.findValue(fileHash, [this, fileHash, savePath](const std::vector<uint8_t>& data, const std::string& filename) {
            if (data.empty()) {
                Logger::warn("File not found in network");
                return;
            }
            FileMetadata meta;
            if (parseMetadata(data, meta)) {
                Logger::info("Received metadata for large file: " + filename +
                             ", size=" + std::to_string(meta.filesize) +
                             ", blocks=" + std::to_string(meta.totalBlocks) +
                             ", blockSize=" + std::to_string(meta.blockSize) +
                             ", hash=" + SHA256::toHex(fileHash));
                Logger::info("Requesting blocks...");
                std::thread([this, fileHash, savePath, filename, meta]() {
                    downloadLarge(fileHash, savePath, filename, meta);
                }).detach();
            } else {
                std::string outFile = savePath + "/" + filename;
                std::ofstream out(outFile, std::ios::binary);
                if (out) {
                    out.write(reinterpret_cast<const char*>(data.data()), data.size());
                    Logger::info("File downloaded to: " + outFile);
                    downloadedFiles_[filename] = fileHash;
                    saveFileList("downloaded.dat", downloadedFiles_);
                    dht_.recordLocalHash(fileHash);
                } else {
                    Logger::error("Failed to open output file: " + outFile);
                }
            }
        });
    }
    
    NodeId getNodeId() const { return nodeId_; }
    RoutingTable& getRoutingTable() { return routingTable_; }

    // accessors for MAC/hardware
    std::string getMacAddress() const { return macAddr_; }
    std::string getHardwareId() const { return hwid_; }

    // return true if the provided address belongs to this host (loopback or any local interface)
    bool isLocalAddress(const sockaddr_in& addr) const {
        if (addr.sin_addr.s_addr == htonl(INADDR_LOOPBACK) || addr.sin_addr.s_addr == htonl(INADDR_ANY)) return true;
#ifdef _WIN32
        IP_ADAPTER_INFO AdapterInfo[16];
        DWORD bufLen = sizeof(AdapterInfo);
        if (GetAdaptersInfo(AdapterInfo, &bufLen) == NO_ERROR) {
            PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
            for (; pAdapterInfo; pAdapterInfo = pAdapterInfo->Next) {
                if (pAdapterInfo->IpAddressList.IpAddress.String[0] == '\0') continue;
                struct in_addr a;
                inet_pton(AF_INET, pAdapterInfo->IpAddressList.IpAddress.String, &a);
                if (a.s_addr == addr.sin_addr.s_addr) return true;
            }
        }
#else
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == 0) {
            for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
                struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
                if (sa->sin_addr.s_addr == addr.sin_addr.s_addr) {
                    freeifaddrs(ifaddr);
                    return true;
                }
            }
            freeifaddrs(ifaddr);
        }
#endif
        return false;
    }

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

    // users.dat persistence helpers
    std::unordered_map<std::string, std::string> loadUsersFile(const std::string& path) {
        std::unordered_map<std::string, std::string> out;
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return out;
        while (true) {
            uint16_t hlen;
            if (!ifs.read(reinterpret_cast<char*>(&hlen), sizeof(hlen))) break;
            std::string hwid(hlen, '\0');
            ifs.read(&hwid[0], hlen);
            uint16_t nlen;
            if (!ifs.read(reinterpret_cast<char*>(&nlen), sizeof(nlen))) break;
            std::string nick(nlen, '\0');
            ifs.read(&nick[0], nlen);
            for (char &c : hwid) c ^= 0xA5;
            for (char &c : nick) c ^= 0xA5;
            out[hwid] = nick;
        }
        return out;
    }

    void saveUsersFile(const std::string &path, const std::unordered_map<std::string, std::string>& map) {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs) return;
        for (const auto &p : map) {
            std::string hwid = p.first;
            std::string nick = p.second;
            for (char &c : hwid) c ^= 0xA5;
            for (char &c : nick) c ^= 0xA5;
            uint16_t hl = static_cast<uint16_t>(hwid.size());
            uint16_t nl = static_cast<uint16_t>(nick.size());
            ofs.write(reinterpret_cast<char*>(&hl), sizeof(hl));
            ofs.write(hwid.data(), hl);
            ofs.write(reinterpret_cast<char*>(&nl), sizeof(nl));
            ofs.write(nick.data(), nl);
        }
    }

    void loadUsersFromDisk() {
        auto users = loadUsersFile(storagePath_ + "/users.dat");
        if (!users.empty()) loadUserRegistry(users);
    }

    void persistUserRegistryToDisk() {
        auto users = getAllUsers();
        saveUsersFile(storagePath_ + "/users.dat", users);
    }

private:
    NodeId nodeId_;
    uint16_t port_;
    std::string storagePath_;

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
    
    // in-memory set of hashes we have recorded to disk (hashes.dat)
    std::unordered_set<Sha256Hash, DHTProtocol::Sha256HashHash, DHTProtocol::Sha256HashEq> knownHashes_;

    // track nodeId to hardware id mapping
    std::unordered_map<std::string, std::string> nodeIdToHwid_;
    
    // track user registry
    std::unordered_map<std::string, DHTProtocol::UserInfo> userRegistry_;
    
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
    
    // helper for file persistence
    void saveFileList(const std::string &filename,
                      const std::unordered_map<std::string, Sha256Hash>& map) {
        std::ofstream ofs(storagePath_ + "/" + filename, std::ios::binary | std::ios::trunc);
        if (!ofs) return;
        for (const auto &p : map) {
            uint16_t nl = static_cast<uint16_t>(p.first.size());
            ofs.write(reinterpret_cast<const char*>(&nl), sizeof(nl));
            ofs.write(p.first.data(), nl);
            ofs.write(reinterpret_cast<const char*>(p.second.data()), 32);
        }
    }

    void loadFileList(const std::string &filename,
                      std::unordered_map<std::string, Sha256Hash>& map) {
        std::ifstream ifs(storagePath_ + "/" + filename, std::ios::binary);
        if (!ifs) return;
        while (true) {
            uint16_t nl;
            if (!ifs.read(reinterpret_cast<char*>(&nl), sizeof(nl))) break;
            std::string name(nl, '\0');
            ifs.read(&name[0], nl);
            Sha256Hash h;
            ifs.read(reinterpret_cast<char*>(h.data()), 32);
            if (ifs.gcount() != 32) break;
            map[name] = h;
        }
    }

    void loadHashesFromDisk() {
        std::ifstream ifs(storagePath_ + "/hashes.dat", std::ios::binary);
        if (!ifs) return;
        while (true) {
            Sha256Hash h;
            if (!ifs.read(reinterpret_cast<char*>(h.data()), 32)) break;
            knownHashes_.insert(h);
            // also mark in dht table so queries will know about it
            dht_.recordLocalHash(h);
        }
    }

    void appendHashToDisk(const Sha256Hash &h) {
        std::ofstream ofs(storagePath_ + "/hashes.dat", std::ios::binary | std::ios::app);
        if (!ofs) return;
        ofs.write(reinterpret_cast<const char*>(h.data()), 32);
    }

    void refreshSavedHashes() {
        auto all = dht_.getKnownHashes();
        for (const auto &h : all) {
            if (knownHashes_.insert(h).second) {
                appendHashToDisk(h);
            }
            dht_.recordLocalHash(h);
        }
    }

    void maintenanceLoop() {
        auto lastRefresh = std::chrono::steady_clock::now();
        auto lastHashSync = std::chrono::steady_clock::now();
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto now = std::chrono::steady_clock::now();
            if (now - lastRefresh > std::chrono::minutes(15)) {
                dht_.refreshBuckets();
                lastRefresh = now;
            }
            if (now - lastHashSync > std::chrono::minutes(1)) {
                syncHashes();
                syncUserRegistry();
                refreshSavedHashes();
                persistUserRegistryToDisk();
                lastHashSync = now;
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
        
        std::vector<uint8_t> blockData;
        if (blockStore_.loadBlockByIndex(fileHash, blockIndex, blockData)) {
            auto packet = PacketSerializer::createSendBlock(nodeId_, fileHash, 
                                                            blockIndex, blockData);
            socket_.sendTo(packet.data(), packet.size(), sender);
        } else {
            Logger::warn("Failed to load requested block " + std::to_string(blockIndex + 1) +
                         " for file " + SHA256::toHex(fileHash) +
                         " (metadata/block missing)");
        }
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
