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
              << "                   (persisted in hashes.dat, updated every minute)\n"
              << "  /downloaded     - Show downloaded files (stored in downloaded.dat)\n"
              << "  /shared         - Show shared files (stored in shared.dat)\n"
              << "  /users          - Show known hardwareID->nickname mappings (users.dat)\n"
              << "  /makeweb <path> - Publish decentralized website from folder (index.html required)\n"
              << "  /websites       - List known decentralized websites\n"
              << "  /web <domain>   - Open decentralized website by domain\n"
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
    // make sure storage directory is available
    std::filesystem::create_directories(storagePath);
    
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
        
        // load registry from disk; node will handle merging/obfuscation
        g_node->loadUsersFromDisk();

        // perform initial user sync with peers as well
        g_node->syncUserRegistry();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::string myHwid = g_node->getHardwareId();
        std::string myNick = g_node->getUserNick(myHwid);
        if (myNick.empty()) {
            std::cout << "Enter your nickname: ";
            std::getline(std::cin, myNick);
            if (myNick.empty()) myNick = "User";
            g_node->announceMyself(myHwid, myNick);
            g_node->persistUserRegistryToDisk();
        } else {
            std::cout << "[*] Using known nickname: " << myNick << std::endl;
            // announce again so others learn our presence
            g_node->announceMyself(myHwid, myNick);
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
                    std::cout << "\n=== Known File Hashes ===\n";
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << " Filename                      | Hash (SHA-256)                                                      | Downloads | Last Seen\n";
                    std::cout << "-------------------------------|--------------------------------------------------------------------|-----------|----------------\n";
                    
                    for (auto &e : stats) {
                        // Format filename (max 30 chars)
                        std::string filename = e.filename;
                        if (filename.length() > 30) {
                            filename = filename.substr(0, 27) + "...";
                        }
                        
                        // Format last seen
                        std::string lastSeen = e.lastSeen;
                        if (lastSeen.length() > 16) {
                            lastSeen = lastSeen.substr(0, 16);
                        }
                        
                        // Print row with full hash
                        std::cout << " " << std::left << std::setw(30) << filename 
                                << " | " << e.hashStr 
                                << " | " << std::right << std::setw(9) << e.downloadCount 
                                << " | " << lastSeen << "\n";
                    }
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << "Total hashes: " << stats.size() << "\n\n";
                }
                
            } else if (line == "/downloaded") {
                auto files = g_node->getDownloadedFiles();
                if (files.empty()) {
                    std::cout << "No downloaded files\n";
                } else {
                    std::cout << "\n=== Downloaded Files ===\n";
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << " Filename                      | Hash (SHA-256)                                                      \n";
                    std::cout << "-------------------------------|--------------------------------------------------------------------\n";
                    
                    for (const auto& f : files) {
                        // Format filename (max 30 chars)
                        std::string filename = f.first;
                        if (filename.length() > 30) {
                            filename = filename.substr(0, 27) + "...";
                        }
                        
                        std::cout << " " << std::left << std::setw(30) << filename 
                                << " | " << f.second << "\n";  // Full hash
                    }
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << "Total files: " << files.size() << "\n\n";
                }
                
            } else if (line == "/shared") {
                auto files = g_node->getSharedFiles();
                if (files.empty()) {
                    std::cout << "No shared files\n";
                } else {
                    std::cout << "\n=== Shared Files ===\n";
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << " Filename                      | Hash (SHA-256)                                                      \n";
                    std::cout << "-------------------------------|--------------------------------------------------------------------\n";
                    
                    for (const auto& f : files) {
                        // Format filename (max 30 chars)
                        std::string filename = f.first;
                        if (filename.length() > 30) {
                            filename = filename.substr(0, 27) + "...";
                        }
                        
                        std::cout << " " << std::left << std::setw(30) << filename 
                                << " | " << f.second << "\n";  // Full hash
                    }
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << "Total files: " << files.size() << "\n\n";
                }
            
            } else if (line == "/users") {
                auto users = g_node->getAllUsers();
                if (users.empty()) {
                    std::cout << "No known users\n";
                } else {
                    std::cout << "\n=== Known Users ===\n";
                    for (const auto &u : users) {
                        std::cout << " " << u.first << " -> " << u.second << "\n";
                    }
                    std::cout << "Total users: " << users.size() << "\n\n";
                }

            } else if (line.substr(0, 9) == "/makeweb ") {
                std::string path = line.substr(9);
                if (g_node->makeWebsite(path)) {
                    std::cout << "Website published successfully\n";
                } else {
                    std::cout << "Failed to publish website\n";
                }

            } else if (line == "/websites") {
                auto sites = g_node->getWebsites();
                if (sites.empty()) {
                    std::cout << "No websites known\n";
                } else {
                    std::cout << "\n=== Known Websites ===\n";
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << " Domain                       | Owner (public key hash)                      | Timestamp\n";
                    std::cout << "-----------------------------|-----------------------------------------------|----------------\n";
                    for (const auto& s : sites) {
                        std::string dom = s.domain;
                        if (dom.size() > 27) dom = dom.substr(0, 24) + "...";
                        std::string owner = s.ownerPublicKey;
                        if (owner.size() > 45) owner = owner.substr(0, 45);
                        std::cout << " " << std::left << std::setw(28) << dom
                                  << " | " << std::left << std::setw(45) << owner
                                  << " | " << s.timestamp << "\n";
                    }
                    std::cout << "--------------------------------------------------------------------------------\n";
                    std::cout << "Total websites: " << sites.size() << "\n\n";
                }

            } else if (line.substr(0, 5) == "/web ") {
                std::string domain = line.substr(5);
                if (!g_node->openWebsite(domain)) {
                    std::cout << "Failed to open website (unknown domain or download/render error)\n";
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
