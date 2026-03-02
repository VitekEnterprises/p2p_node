#include "udp_socket.hpp"
#include "../types.hpp"
#include <cerrno>
#include <cstring>

namespace p2p {

UDPSocket::UDPSocket(uint16_t port) : port_(port) {}

UDPSocket::~UDPSocket() {
    stop();
    if (sockfd_ != INVALID_SOCKET) {
        closesocket(sockfd_);
    }
}

bool UDPSocket::init() {
#ifdef _WIN32
    // Initialize Winsock
    static bool winsockInit = false;
    if (!winsockInit) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Logger::error("WSAStartup failed");
            return false;
        }
        winsockInit = true;
    }
#endif

    sockfd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd_ == INVALID_SOCKET) {
#ifdef _WIN32
        Logger::error("Failed to create socket: " + std::to_string(WSAGetLastError()));
#else
        Logger::error("Failed to create socket: " + std::string(strerror(errno)));
#endif
        return false;
    }
    
    // Allow reuse
    int reuse = 1;
#ifdef _WIN32
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse)) < 0) {
#else
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, 
                   &reuse, sizeof(reuse)) < 0) {
#endif
        Logger::warn("Failed to set SO_REUSEADDR");
    }

    // enable broadcast on socket
    int broadcast = 1;
#ifdef _WIN32
    setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
#else
    setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
#endif
    
    // Bind
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        Logger::error("Failed to bind socket: " + std::to_string(WSAGetLastError()));
#else
        Logger::error("Failed to bind socket: " + std::string(strerror(errno)));
#endif
        closesocket(sockfd_);
        sockfd_ = INVALID_SOCKET;
        return false;
    }
    
    setNonBlocking(sockfd_);
    Logger::info("UDP socket initialized on port " + std::to_string(port_));
    return true;
}

void UDPSocket::startReceiveLoop() {
    if (!running_) {
        running_ = true;
        receiveThread_ = std::thread(&UDPSocket::receiveLoop, this);
    }
}

void UDPSocket::stop() {
    running_ = false;
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

bool UDPSocket::sendTo(const uint8_t* data, size_t len, const sockaddr_in& dest) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    
    int sent = sendto(sockfd_, reinterpret_cast<const char*>(data), 
                      static_cast<int>(len), 0,
                      (struct sockaddr*)&dest, sizeof(dest));
    
    if (sent == SOCKET_ERROR) {
#ifdef _WIN32
        int err = WSAGetLastError();
        // ignore "destination address required" and nonblocking
        if (err != WSAEWOULDBLOCK && err != 10040) {
            Logger::error("Send failed: " + std::to_string(err));
        }
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EDESTADDRREQ) {
            Logger::error("Send failed: " + std::string(strerror(errno)));
        }
#endif
        return false;
    }
    
    return static_cast<size_t>(sent) == len;
}

void UDPSocket::setPacketHandler(PacketHandler handler) {
    packetHandler_ = handler;
}

void UDPSocket::receiveLoop() {
    uint8_t buffer[65535];
    sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);
    
    Logger::debug("Receive loop started");
    
    while (running_) {
#ifdef _WIN32
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(0, &readfds, NULL, NULL, &tv);
        
        if (activity == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEINTR) {
                // log error
            }
            continue;
        }
        
        if (FD_ISSET(sockfd_, &readfds)) {
            int received = recvfrom(sockfd_, reinterpret_cast<char*>(buffer), 
                                    sizeof(buffer), 0,
                                    (struct sockaddr*)&senderAddr, &senderLen);
            
            if (received > 0) {
                if (packetHandler_) {
                    packetHandler_(buffer, received, senderAddr);
                }
            }
        }
#else
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(sockfd_ + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0) {
            if (errno != EINTR) {
                // log error
            }
            continue;
        }
        
        if (FD_ISSET(sockfd_, &readfds)) {
            ssize_t received = recvfrom(sockfd_, buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&senderAddr, &senderLen);
            
            if (received > 0) {
                if (packetHandler_) {
                    packetHandler_(buffer, received, senderAddr);
                }
            }
        }
#endif
        
        // Process send queue if needed
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            while (!sendQueue_.empty()) {
                auto& elem = sendQueue_.front();
                const auto& data = elem.first;
                const auto& addr = elem.second;
                sendTo(data.data(), data.size(), addr);
                sendQueue_.pop();
            }
        }
    }
    
    Logger::debug("Receive loop stopped");
}

bool UDPSocket::setNonBlocking(SOCKET fd) {
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

} // namespace p2p
