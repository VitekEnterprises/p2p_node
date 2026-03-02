#pragma once
#include "../types.hpp"
#include "kbucket.hpp"
#include "xor_distance.hpp"
#include <array>
#include <memory>
#include <vector>
#include <algorithm>

namespace p2p {

class RoutingTable {
public:
    static constexpr int BITS = 256;
    
    RoutingTable(const NodeId& selfId) : selfId_(selfId) {
        for (int i = 0; i < BITS; ++i) {
            buckets_[i] = std::make_unique<KBucket>();
        }
    }
    
    bool addNode(const NodeInfo& node) {
        if (node.id == selfId_) return false;
        
        int index = getBucketIndex(node.id);
        return buckets_[index]->addNode(node);
    }
    
    void removeNode(const NodeId& id) {
        if (id == selfId_) return;
        int index = getBucketIndex(id);
        buckets_[index]->removeNode(id);
    }
    
    NodeInfo* findNode(const NodeId& id) {
        if (id == selfId_) return nullptr;
        int index = getBucketIndex(id);
        return buckets_[index]->findNode(id);
    }
    
    std::vector<NodeInfo> findClosest(const NodeId& target, size_t count = 8) {
        std::vector<NodeInfo> result;
        
        int startBucket = getBucketIndex(target);
        
        for (int offset = 0; offset < BITS && result.size() < count * 2; ++offset) {
            if (startBucket + offset < BITS) {
                auto nodes = buckets_[startBucket + offset]->getNodes(KBucket::K);
                result.insert(result.end(), nodes.begin(), nodes.end());
            }
            
            if (offset > 0 && startBucket - offset >= 0) {
                auto nodes = buckets_[startBucket - offset]->getNodes(KBucket::K);
                result.insert(result.end(), nodes.begin(), nodes.end());
            }
        }
        
        std::sort(result.begin(), result.end(),
                  [&](const NodeInfo& a, const NodeInfo& b) {
                      return XORDistance::compare(a.id, b.id, target);
                  });
        
        std::set<NodeId> seen;
        std::vector<NodeInfo> unique;
        for (const auto& node : result) {
            if (seen.find(node.id) == seen.end()) {
                seen.insert(node.id);
                unique.push_back(node);
            }
        }
        
        if (unique.size() > count) {
            unique.resize(count);
        }
        
        return unique;
    }
    
    std::vector<NodeInfo> getAllNodes() {
        std::vector<NodeInfo> result;
        for (int i = 0; i < BITS; ++i) {
            auto nodes = buckets_[i]->getNodes(KBucket::K);
            result.insert(result.end(), nodes.begin(), nodes.end());
        }
        return result;
    }

    std::vector<NodeInfo> getAllNodes() const {
        std::vector<NodeInfo> result;
        for (int i = 0; i < BITS; ++i) {
            auto nodes = buckets_[i]->getNodes(KBucket::K);
            result.insert(result.end(), nodes.begin(), nodes.end());
        }
        return result;
    }
    
    size_t totalNodes() const {
        size_t total = 0;
        for (int i = 0; i < BITS; ++i) {
            total += buckets_[i]->size();
        }
        return total;
    }

private:
    NodeId selfId_;
    std::array<std::unique_ptr<KBucket>, BITS> buckets_;
    mutable std::mutex mutex_;
    
    int getBucketIndex(const NodeId& id) const {
        return XORDistance::leadingBits(selfId_, id);
    }
};

} // namespace p2p
