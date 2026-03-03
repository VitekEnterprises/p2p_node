#pragma once

#include "../types.hpp"
#include "../util/sha256.hpp"
#include <string>
#include <vector>

namespace p2p {

struct WebsiteMetadata {
    std::string domain;
    std::string owner_public_key;
    uint64_t timestamp{0};
    std::string signature;
    std::string root_file_hash;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const std::vector<uint8_t>& data, WebsiteMetadata& out);

    std::vector<uint8_t> serializeListEntry() const;
    static bool deserializeListEntry(const std::vector<uint8_t>& data, size_t& pos, WebsiteMetadata& out);

    std::string signingPayload() const;
};

std::string signWebsiteMetadata(const WebsiteMetadata& meta, const std::string& privateKey, const std::string& publicKey);
bool verifyWebsiteMetadata(const WebsiteMetadata& meta);
bool isMetadataBetter(const WebsiteMetadata& candidate, const WebsiteMetadata& current);

} // namespace p2p
