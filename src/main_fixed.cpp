#include "node.hpp"
#include <iostream>
#include <csignal>
#include <memory>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#endif
#include <cstring>

std::unique_ptr<p2p::P2PNode> g_node;

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\nShutting down..." << std::endl;
        if (g_node) {
            g_node->stop();
        }
        exit(0);
    }
}

void printHelp() {
    std::cout << "P2P Node Commands:\n"
              << "  /help           - Show this help\n"
              << "  /status         - Show node status\n"
              << "  /nodes          - Show routing table size\n"
              << "  /hashes         - Show all shared file hashes with download counts\n"
              << "  /downloaded     - Show downloaded files\n"
              << "  /shared         - Show shared files\n"
              << "  /find <hash>    - Find who online has a specific file\n"
              << "  /share <file> [public|private]   - Share a file\n"
              << "  /download <hash> - Download a file\n"
              << "  /msg <message>   - Send chat message\n"
              << "  /exit           - Exit\n";
}

uint32_t ipToInt(const char* ip) {
    uint32_t r;
    inet_pton(AF_INET, ip, &r);
    return ntohl(r);
}

std::string intToIp(uint32_t ip) {
    struct in_addr a;
    a.s_addr = htonl(ip);
    return std::string(inet_ntoa(a));
}

std::vector<std::string> computeBroadcasts(uint16_t port) {
    std::vector<std::string> out;
#ifdef _WIN32
    IP_ADAPTER_INFO AdapterInfo[16];
    DWORD bufLen = sizeof(AdapterInfo);
    if (GetAdaptersInfo(AdapterInfo, &bufLen) == NO_ERROR) {
        PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
        while (pAdapterInfo) {
            if (pAdapterInfo->IpAddressList.IpAddress.String[0] != '\0') {
                uint32_t ip = ipToInt(pAdapterInfo->IpAddressList.IpAddress.String);
                uint32_t mask = ipToInt(pAdapterInfo->IpAddressList.IpMask.String);
                uint32_t b = (ip & mask) | (~mask);
                out.push_back(intToIp(b) + ":" + std::to_string(port));
            }
            pAdapterInfo = pAdapterInfo->Next;
        }
    }
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
            struct sockaddr_in *nm = (struct sockaddr_in*)ifa->ifa_netmask;
            uint32_t ip = ntohl(sa->sin_addr.s_addr);
            uint32_t mask = ntohl(nm->sin_addr.s_addr);
            uint32_t b = (ip & mask) | (~mask);
            out.push_back(intToIp(b) + ":" + std::to_string(port));
        }
        freeifaddrs(ifaddr);
    }
#endif
    return out;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    uint16_t port = 6881;
    std::string storagePath = "./storage";
    
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) storagePath = argv[2];
    
    try {
        auto node = std::make_unique<p2p::P2PNode>(port, storagePath);
        g_node = std::move(node);
        
        if (!g_node->init()) {
            std::cerr << "Failed to initialize node" << std::endl;
            return 1;
        }
        
        // Bootstrap nodes: either provided by command line or try automatic discovery
        std::vector<std::string> bootstrapNodes;
        if (argc > 3) {
            bootstrapNodes.push_back(argv[3]);
        } else {
            // attempt discovery by computing local broadcast addresses
            auto bcasts = computeBroadcasts(port);
            for (auto &b : bcasts) bootstrapNodes.push_back(b);
            // also include generic broadcast as fallback
            bootstrapNodes.push_back("255.255.255.255:" + std::to_string(port));
        }

        g_node->bootstrap(bootstrapNodes);

        // give a short period for any replies to arrive
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (g_node->getRoutingTable().totalNodes() == 0) {
            // nothing found, ask user for manual bootstrap entry
            std::cout << "No peers discovered automatically. Enter bootstrap node (ip:port) or leave empty: ";
            std::string manual;
            std::getline(std::cin, manual);
            if (!manual.empty()) {
                bootstrapNodes.clear();
                bootstrapNodes.push_back(manual);
                g_node->bootstrap(bootstrapNodes);
            }
        }

        g_node->start();
        
        std::cout << "\n[*] P2P Node running on port " << port << std::endl;
        std::cout << "[*] Node ID: " << p2p::SHA256::toHex(g_node->getNodeId()) << std::endl;
        std::cout << "[*] Storage path: " << storagePath << std::endl;
        std::cout << "[*] MAC address: " << g_node->getMacAddress() << std::endl;
        std::cout << "[*] Hardware ID: " << g_node->getHardwareId() << std::endl;
        
        // load user registry from persistence (simple format: hwid|nick)
        std::string userFile = storagePath + "/users.txt";
        std::unordered_map<std::string, std::string> savedUsers;
        try {
            std::ifstream uf(userFile);
            if (uf.good()) {
                std::string line;
                while (std::getline(uf, line)) {
                    size_t sep = line.find('|');
                    if (sep != std::string::npos) {
                        std::string hwid = line.substr(0, sep);
                        std::string nick = line.substr(sep + 1);
                        savedUsers[hwid] = nick;
                    }
                }
                g_node->loadUserRegistry(savedUsers);
            }
        } catch (...) {
            // ignore load errors
        }
        
        // synchronize with peers and resolve nickname
        g_node->syncUserRegistry();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::string myHwid = g_node->getHardwareId();
        std::string myNick = g_node->getUserNick(myHwid);
        
        if (myNick.empty()) {
            // check local saved users
            auto it = savedUsers.find(myHwid);
            if (it != savedUsers.end()) {
                myNick = it->second;
                std::cout << "[*] Loaded nickname from history: " << myNick << std::endl;
            } else {
                // ask user to set one
                std::cout << "Enter your nickname: ";
                std::getline(std::cin, myNick);
                if (myNick.empty()) myNick = "User";
            }
        } else {
            std::cout << "[*] Using known nickname: " << myNick << std::endl;
        }
        
        // announce ourselves
        g_node->announceMyself(myHwid, myNick);
        savedUsers[myHwid] = myNick;
        
        // save user registry
        try {
            std::ofstream uf(userFile);
            for (const auto& p : savedUsers) {
                uf << p.first << "|" << p.second << "\n";
            }
        } catch (...) {
            // ignore save errors
        }
        
        // synchronize hash table with peers
        g_node->syncHashes();
        printHelp();
        
        // Interactive console
        std::string line;
        while (true) {
            std::cout << "> ";
            std::getline(std::cin, line);
            
            if (line == "/exit") {
                break;
            } else if (line == "/help") {
                printHelp();
            } else if (line == "/status") {
                std::cout << "Node is running. Total nodes in routing table: " 
                         << g_node->getRoutingTable().totalNodes() << std::endl;
            } else if (line == "/nodes") {
                std::cout << "Routing table size: " 
                         << g_node->getRoutingTable().totalNodes() << std::endl;
            } else if (line == "/hashes") {
                auto stats = g_node->getHashStats();
                if (stats.empty()) {
                    std::cout << "No known hashes\n";
                } else {
                    std::cout << "Known file hashes (filename, downloads, last seen):\n";
                    for (auto &e : stats) {
                        std::cout << "  " << e.filename << " | " << e.hashStr.substr(0, 65) << "..." 
                                 << " | downloads=" << e.downloadCount 
                                 << " | " << e.lastSeen << "\n";
                    }
                }
            } else if (line == "/downloaded") {
                auto files = g_node->getDownloadedFiles();
                if (files.empty()) {
                    std::cout << "No downloaded files\n";
                } else {
                    std::cout << "Downloaded files:\n";
                    for (const auto& f : files) {
                        std::cout << "  " << f.first << " (hash: " << f.second.substr(0, 16) << "...)\n";
                    }
                }
            } else if (line == "/shared") {
                auto files = g_node->getSharedFiles();
                if (files.empty()) {
                    std::cout << "No shared files\n";
                } else {
                    std::cout << "Shared files:\n";
                    for (const auto& f : files) {
                        std::cout << "  " << f.first << " (hash: " << f.second.substr(0, 16) << "...)\n";
                    }
                }
            } else if (line.substr(0, 6) == "/find ") {
                std::string hashStr = line.substr(6);
                auto hash = p2p::SHA256::fromHex(hashStr);
                auto online = g_node->findOnlineOwners(hash);
                if (online.empty()) {
                    std::cout << "No online peers have this file\n";
                } else {
                    std::cout << "Online peers with this file:\n";
                    for (const auto& addr : online) {
                        std::cout << "  " << addr << "\n";
                    }
                }
            } else if (line.substr(0, 5) == "/msg ") {
                std::string msg = line.substr(5);
                g_node->sendChat(msg);
                std::cout << "[CHAT] me: " << msg << std::endl;
            } else if (line.substr(0, 7) == "/share ") {
                // syntax: /share <file> [public|private]
                std::istringstream iss(line.substr(7));
                std::string filepath, mode;
                iss >> filepath >> mode;
                bool isPublic = (mode == "public");
                if (g_node->shareFile(filepath, isPublic)) {
                    std::cout << "File shared successfully" << std::endl;
                } else {
                    std::cout << "Failed to share file" << std::endl;
                }
            } else if (line.substr(0, 10) == "/download ") {
                std::string hashStr = line.substr(10);
                auto hash = p2p::SHA256::fromHex(hashStr);
                std::filesystem::create_directories("./downloads");
                g_node->downloadFile(hash, "./downloads");
            } else if (!line.empty()) {
                std::cout << "Unknown command. Type /help for help." << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    if (g_node) {
        g_node->stop();
    }
    
    return 0;
}
