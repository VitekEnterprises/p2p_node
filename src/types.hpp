//=============================================================================
// types.hpp
//=============================================================================
#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <netdb.h>
#endif

#include <cstdint>
#include <array>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <set>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <ctime>
#include <queue>
#include <memory>
#include <filesystem>

namespace p2p {

using NodeId = std::array<uint8_t, 32>;
using Sha256Hash = std::array<uint8_t, 32>;

struct NodeInfo {
    NodeId id;
    sockaddr_in addr;
    std::chrono::steady_clock::time_point lastSeen;
    uint32_t failedPings{0};
    bool isReachable{true};
    
    bool operator==(const NodeInfo& other) const {
        return id == other.id;
    }
};

struct Block {
    Sha256Hash hash;
    std::vector<uint8_t> data;
    uint32_t index;
};

struct FileMetadata {
    Sha256Hash fileHash;
    uint32_t totalBlocks;
    std::vector<Sha256Hash> blockHashes;
    std::string filename;
    uint64_t filesize;
};

// Logger
class Logger {
public:
    enum Level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
    
    static void logMessage(Level level, const std::string& message) {
        std::string levelStr;
        switch(level) {
            case LOG_DEBUG: levelStr = "DEBUG"; break;
            case LOG_INFO:  levelStr = "INFO"; break;
            case LOG_WARN:  levelStr = "WARN"; break;
            case LOG_ERROR: levelStr = "ERROR"; break;
        }
        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::string timeStr = std::ctime(&now_c);
        timeStr.pop_back(); // Remove newline
        
        std::cout << "[" << timeStr << "] [" << levelStr << "] " << message << std::endl;
    }
    
    static void debug(const std::string& msg) { logMessage(LOG_DEBUG, msg); }
    static void info(const std::string& msg) { logMessage(LOG_INFO, msg); }
    static void warn(const std::string& msg) { logMessage(LOG_WARN, msg); }
    static void error(const std::string& msg) { logMessage(LOG_ERROR, msg); }
};

} // namespace p2p
