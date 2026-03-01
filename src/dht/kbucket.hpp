#pragma once
#include "../types.hpp"
#include "xor_distance.hpp"
#include <list>
#include <mutex>
#include <algorithm>

namespace p2p {

class KBucket {
public:
    static constexpr size_t K = 8;
    
    KBucket() = default;
    
    bool addNode(const NodeInfo& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
                               [&](const NodeInfo& n) { return n.id == node.id; });
        
        if (it != nodes_.end()) {
            nodes_.splice(nodes_.begin(), nodes_, it);
            it->lastSeen = std::chrono::steady_clock::now();
            it->failedPings = 0;
            return true;
        }
        
        if (nodes_.size() < K) {
            nodes_.push_front(node);
            return true;
        }
        
        for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
            auto age = std::chrono::steady_clock::now() - it->lastSeen;
            if (age > std::chrono::minutes(15) || it->failedPings >= 3) {
                nodes_.erase(it);
                nodes_.push_front(node);
                return true;
            }
        }
        
        return false;
    }
    
    void removeNode(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodes_.remove_if([&](const NodeInfo& n) { return n.id == id; });
    }
    
    NodeInfo* findNode(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
                               [&](const NodeInfo& n) { return n.id == id; });
        if (it != nodes_.end()) {
            return &(*it);
        }
        return nullptr;
    }
    
    std::vector<NodeInfo> getNodes(size_t count = K) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NodeInfo> result;
        size_t n = (count < nodes_.size()) ? count : nodes_.size();
        auto it = nodes_.begin();
        for (size_t i = 0; i < n; ++i, ++it) {
            result.push_back(*it);
        }
        return result;
    }
    
    std::vector<NodeInfo> getClosest(const NodeId& target, size_t count = K) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NodeInfo> result(nodes_.begin(), nodes_.end());
        
        // TODO: Sort by XOR distance to target
        // For now, just return the requested count
        if (result.size() > count) {
            result.resize(count);
        }
        
        return result;
    }
    
    void onResponse(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
                               [&](const NodeInfo& n) { return n.id == id; });
        if (it != nodes_.end()) {
            it->lastSeen = std::chrono::steady_clock::now();
            it->failedPings = 0;
            nodes_.splice(nodes_.begin(), nodes_, it);
        }
    }
    
    void onTimeout(const NodeId& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
                               [&](const NodeInfo& n) { return n.id == id; });
        if (it != nodes_.end()) {
            it->failedPings++;
            if (it->failedPings >= 3) {
                nodes_.erase(it);
            }
        }
    }
    
    size_t size() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_.size(); 
    }
    
    bool isFull() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_.size() >= K; 
    }

private:
    std::list<NodeInfo> nodes_;
    mutable std::mutex mutex_;
};

} // namespace p2p
