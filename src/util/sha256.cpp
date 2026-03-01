#include "sha256.hpp"
#include <cstring>

namespace p2p {

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

SHA256::SHA256() : bufferLen(0), bitCount(0) {
    h[0] = 0x6a09e667;
    h[1] = 0xbb67ae85;
    h[2] = 0x3c6ef372;
    h[3] = 0xa54ff53a;
    h[4] = 0x510e527f;
    h[5] = 0x9b05688c;
    h[6] = 0x1f83d9ab;
    h[7] = 0x5be0cd19;
}

void SHA256::update(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        buffer[bufferLen++] = data[i];
        bitCount += 8;
        
        if (bufferLen == 64) {
            transform(buffer);
            bufferLen = 0;
        }
    }
}

void SHA256::update(const std::vector<uint8_t>& data) {
    update(data.data(), data.size());
}

void SHA256::update(const std::string& data) {
    update(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
}

Sha256Hash SHA256::finalize() {
    // Padding
    buffer[bufferLen++] = 0x80;
    
    if (bufferLen > 56) {
        while (bufferLen < 64) buffer[bufferLen++] = 0;
        transform(buffer);
        bufferLen = 0;
    }
    
    while (bufferLen < 56) buffer[bufferLen++] = 0;
    
    // Append length
    for (int i = 0; i < 8; ++i) {
        buffer[56 + i] = (bitCount >> (56 - i * 8)) & 0xff;
    }
    
    transform(buffer);
    
    // Produce final hash
    Sha256Hash hash;
    for (int i = 0; i < 8; ++i) {
        hash[i*4] = (h[i] >> 24) & 0xff;
        hash[i*4+1] = (h[i] >> 16) & 0xff;
        hash[i*4+2] = (h[i] >> 8) & 0xff;
        hash[i*4+3] = h[i] & 0xff;
    }
    
    return hash;
}

void SHA256::transform(const uint8_t* block) {
    uint32_t w[64];
    
    // Prepare message schedule
    for (int i = 0; i < 16; ++i) {
        w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) |
               (block[i*4+2] << 8) | block[i*4+3];
    }
    
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    
    // Initialize working variables
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    
    // Main loop
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        
        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    
    // Update hash values
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

Sha256Hash SHA256::hash(const uint8_t* data, size_t len) {
    SHA256 sha;
    sha.update(data, len);
    return sha.finalize();
}

Sha256Hash SHA256::hash(const std::vector<uint8_t>& data) {
    return hash(data.data(), data.size());
}

Sha256Hash SHA256::hash(const std::string& data) {
    return hash(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
}

std::string SHA256::toHex(const Sha256Hash& hash) {
    std::stringstream ss;
    for (size_t i = 0; i < hash.size(); ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(hash[i]);
    }
    return ss.str();
}

Sha256Hash SHA256::fromHex(const std::string& hex) {
    Sha256Hash hash;
    for (size_t i = 0; i < 32 && i*2 < hex.length(); ++i) {
        std::string byteStr = hex.substr(i*2, 2);
        hash[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
    }
    return hash;
}

uint32_t SHA256::rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

uint32_t SHA256::ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ ((~x) & z);
}

uint32_t SHA256::maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

uint32_t SHA256::sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

uint32_t SHA256::sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

uint32_t SHA256::Gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

uint32_t SHA256::Gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

} // namespace p2p
