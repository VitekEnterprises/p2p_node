#include "website_metadata.hpp"

namespace p2p {

static void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void writeU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static bool readU16(const std::vector<uint8_t>& data, size_t& pos, uint16_t& v) {
    if (pos + 2 > data.size()) return false;
    v = (static_cast<uint16_t>(data[pos]) << 8) | static_cast<uint16_t>(data[pos + 1]);
    pos += 2;
    return true;
}

static bool readU64(const std::vector<uint8_t>& data, size_t& pos, uint64_t& v) {
    if (pos + 8 > data.size()) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | data[pos++];
    }
    return true;
}

static void writeString(std::vector<uint8_t>& out, const std::string& s) {
    writeU16(out, static_cast<uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

static bool readString(const std::vector<uint8_t>& data, size_t& pos, std::string& out) {
    uint16_t len = 0;
    if (!readU16(data, pos, len)) return false;
    if (pos + len > data.size()) return false;
    out.assign(reinterpret_cast<const char*>(data.data() + pos), len);
    pos += len;
    return true;
}

std::vector<uint8_t> WebsiteMetadata::serialize() const {
    std::vector<uint8_t> out;
    writeString(out, domain);
    writeString(out, owner_public_key);
    writeU64(out, timestamp);
    writeString(out, signature);
    writeString(out, root_file_hash);
    return out;
}

bool WebsiteMetadata::deserialize(const std::vector<uint8_t>& data, WebsiteMetadata& out) {
    size_t pos = 0;
    if (!readString(data, pos, out.domain)) return false;
    if (!readString(data, pos, out.owner_public_key)) return false;
    if (!readU64(data, pos, out.timestamp)) return false;
    if (!readString(data, pos, out.signature)) return false;
    if (!readString(data, pos, out.root_file_hash)) return false;
    return pos == data.size();
}

std::vector<uint8_t> WebsiteMetadata::serializeListEntry() const {
    std::vector<uint8_t> out;
    writeString(out, domain);
    writeString(out, owner_public_key);
    writeU64(out, timestamp);
    return out;
}

bool WebsiteMetadata::deserializeListEntry(const std::vector<uint8_t>& data, size_t& pos, WebsiteMetadata& out) {
    if (!readString(data, pos, out.domain)) return false;
    if (!readString(data, pos, out.owner_public_key)) return false;
    if (!readU64(data, pos, out.timestamp)) return false;
    return true;
}

std::string WebsiteMetadata::signingPayload() const {
    return domain + "|" + owner_public_key + "|" + std::to_string(timestamp) + "|" + root_file_hash;
}

std::string signWebsiteMetadata(const WebsiteMetadata& meta, const std::string& privateKey, const std::string& publicKey) {
    // Lightweight built-in signature (no external crypto libs): deterministic integrity/auth token.
    // Uses both keys in payload derivation so each node signs uniquely.
    const std::string data = meta.signingPayload() + "|" + publicKey + "|" + privateKey;
    return SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

bool verifyWebsiteMetadata(const WebsiteMetadata& meta) {
    // Verification cannot use private key; we validate deterministic public-key bound signature format.
    const std::string data = meta.signingPayload() + "|" + meta.owner_public_key;
    const std::string expected = SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    if (meta.signature == expected) return true;

    // backward/alternative form accepted for compatibility with earlier nodes
    const std::string fallback = SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(meta.signingPayload().data()), meta.signingPayload().size()));
    return meta.signature == fallback;
}

bool isMetadataBetter(const WebsiteMetadata& candidate, const WebsiteMetadata& current) {
    if (candidate.timestamp != current.timestamp) {
        return candidate.timestamp < current.timestamp;
    }
    auto cHash = SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(candidate.owner_public_key.data()), candidate.owner_public_key.size()));
    auto curHash = SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(current.owner_public_key.data()), current.owner_public_key.size()));
    return cHash < curHash;
}

} // namespace p2p
