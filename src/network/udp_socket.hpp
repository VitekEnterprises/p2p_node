#pragma once
#include "../types.hpp"
#include <queue>
#include <functional>

namespace p2p {

class UDPSocket {
public:
    using PacketHandler = std::function<void(const uint8_t* data, size_t len, 
                                              const sockaddr_in& sender)>;

    UDPSocket(uint16_t port);
    ~UDPSocket();

    bool init();
    void startReceiveLoop();
    void stop();
    
    bool sendTo(const uint8_t* data, size_t len, const sockaddr_in& dest);
    void setPacketHandler(PacketHandler handler);
    
    uint16_t getPort() const { return port_; }

private:
    SOCKET sockfd_{INVALID_SOCKET};
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread receiveThread_;
    PacketHandler packetHandler_;
    
    std::mutex sendMutex_;
    std::queue<std::pair<std::vector<uint8_t>, sockaddr_in>> sendQueue_;
    
    void receiveLoop();
    bool setNonBlocking(SOCKET fd);
};

} // namespace p2p
