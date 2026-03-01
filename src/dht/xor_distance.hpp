#pragma once
#include "../types.hpp"

namespace p2p {

class XORDistance {
public:
    static NodeId xor_distance(const NodeId& a, const NodeId& b) {
        NodeId result;
        for (size_t i = 0; i < 32; ++i) {
            result[i] = a[i] ^ b[i];
        }
        return result;
    }
    
    static bool compare(const NodeId& a, const NodeId& b, const NodeId& target) {
        NodeId distA = xor_distance(a, target);
        NodeId distB = xor_distance(b, target);
        return distA < distB;
    }
    
    static int leadingBits(const NodeId& a, const NodeId& b) {
        NodeId xord = xor_distance(a, b);
        int leading = 0;
        for (size_t i = 0; i < 32; ++i) {
            if (xord[i] == 0) {
                leading += 8;
            } else {
                uint8_t byte = xord[i];
                for (int j = 7; j >= 0; --j) {
                    if (byte & (1 << j)) {
                        return leading;
                    }
                    leading++;
                }
            }
        }
        return leading;
    }
};

} // namespace p2p
