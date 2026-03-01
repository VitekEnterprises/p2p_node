#pragma once
#include "../types.hpp"
#include <random>

namespace p2p {

class Random {
public:
    static void init() {
        getEngine().seed(std::random_device{}());
    }
    
    static uint32_t uint32() {
        std::uniform_int_distribution<uint32_t> dist;
        return dist(getEngine());
    }
    
    static void bytes(uint8_t* data, size_t len) {
        std::uniform_int_distribution<uint32_t> dist(0, 255);
        for (size_t i = 0; i < len; ++i) {
            data[i] = static_cast<uint8_t>(dist(getEngine()));
        }
    }
    
    static NodeId generateNodeId() {
        NodeId id;
        bytes(id.data(), id.size());
        return id;
    }

private:
    static std::mt19937& getEngine() {
        static std::mt19937 engine(std::random_device{}());
        return engine;
    }
};

} // namespace p2p
