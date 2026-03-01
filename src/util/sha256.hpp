#pragma once
#include "../types.hpp"
#include <string>
#include <vector>

namespace p2p {

class SHA256 {
public:
    SHA256();
    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);
    void update(const std::string& data);
    Sha256Hash finalize();
    
    static Sha256Hash hash(const uint8_t* data, size_t len);
    static Sha256Hash hash(const std::vector<uint8_t>& data);
    static Sha256Hash hash(const std::string& data);
    
    static std::string toHex(const Sha256Hash& hash);
    static Sha256Hash fromHex(const std::string& hex);

private:
    uint32_t h[8];
    uint8_t buffer[64];
    size_t bufferLen;
    uint64_t bitCount;
    
    void transform(const uint8_t* block);
    static uint32_t rotr(uint32_t x, uint32_t n);
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z);
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z);
    static uint32_t sigma0(uint32_t x);
    static uint32_t sigma1(uint32_t x);
    static uint32_t Gamma0(uint32_t x);
    static uint32_t Gamma1(uint32_t x);
};

} // namespace p2p
