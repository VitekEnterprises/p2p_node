#include "web_manager.hpp"
#include "../util/random.hpp"
#include "../util/sha256.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace p2p {

static std::string randomHex(size_t bytesCount) {
    std::vector<uint8_t> b(bytesCount);
    Random::bytes(b.data(), b.size());
    return SHA256::toHex(SHA256::hash(b));
}

static void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static bool readU16(const std::vector<uint8_t>& data, size_t& pos, uint16_t& v) {
    if (pos + 2 > data.size()) return false;
    v = (static_cast<uint16_t>(data[pos]) << 8) | static_cast<uint16_t>(data[pos + 1]);
    pos += 2;
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

WebManager::WebManager(const std::string& storagePath)
    : storagePath_(storagePath),
      identityPath_((std::filesystem::path(storagePath_) / "identity.dat").string()),
      websitesPath_((std::filesystem::path(storagePath_) / "websites.dat").string()) {
}

bool WebManager::init() {
    std::filesystem::create_directories(storagePath_);
    std::filesystem::create_directories(std::filesystem::path(storagePath_) / "websites");
    if (!loadIdentity()) return false;
    loadWebsites();
    return true;
}

bool WebManager::loadIdentity() {
    std::ifstream ifs(identityPath_, std::ios::binary);
    if (ifs) {
        std::getline(ifs, publicKey_);
        std::getline(ifs, privateKey_);
        if (!publicKey_.empty() && !privateKey_.empty()) return true;
    }

    privateKey_ = randomHex(32);
    publicKey_ = SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(privateKey_.data()), privateKey_.size()));
    return saveIdentity();
}

bool WebManager::saveIdentity() const {
    std::ofstream ofs(identityPath_, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << publicKey_ << "\n" << privateKey_ << "\n";
    return true;
}

void WebManager::loadWebsites() {
    std::lock_guard<std::mutex> lock(mutex_);
    websites_.clear();

    std::ifstream ifs(websitesPath_, std::ios::binary);
    if (!ifs) return;

    while (true) {
        uint16_t len = 0;
        if (!ifs.read(reinterpret_cast<char*>(&len), sizeof(len))) break;
        std::vector<uint8_t> buffer(len);
        if (!ifs.read(reinterpret_cast<char*>(buffer.data()), len)) break;

        WebsiteMetadata meta;
        if (!WebsiteMetadata::deserialize(buffer, meta)) continue;
        if (!verifyWebsiteMetadata(meta)) continue;

        auto it = websites_.find(meta.domain);
        if (it == websites_.end() || isMetadataBetter(meta, it->second)) {
            websites_[meta.domain] = meta;
        }
    }
}

void WebManager::saveWebsites() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream ofs(websitesPath_, std::ios::binary | std::ios::trunc);
    if (!ofs) return;

    for (const auto& kv : websites_) {
        auto data = kv.second.serialize();
        uint16_t len = static_cast<uint16_t>(data.size());
        ofs.write(reinterpret_cast<const char*>(&len), sizeof(len));
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

bool WebManager::mergeMetadata(const WebsiteMetadata& incoming, bool persist) {
    if (!verifyWebsiteMetadata(incoming)) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = websites_.find(incoming.domain);
    bool changed = false;
    if (it == websites_.end() || isMetadataBetter(incoming, it->second)) {
        websites_[incoming.domain] = incoming;
        changed = true;
    }

    if (changed && persist) {
        std::ofstream ofs(websitesPath_, std::ios::binary | std::ios::trunc);
        if (ofs) {
            for (const auto& kv : websites_) {
                auto data = kv.second.serialize();
                uint16_t len = static_cast<uint16_t>(data.size());
                ofs.write(reinterpret_cast<const char*>(&len), sizeof(len));
                ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
            }
        }
    }

    return changed;
}

bool WebManager::makeWeb(const std::string& folderPath,
                         const std::function<bool(const std::string&, bool, Sha256Hash&)>& publishFile) {
    std::string indexPath;
    if (!WebFileStore::findIndexHtml(folderPath, indexPath)) {
        Logger::error("Web folder must contain index.html");
        return false;
    }

    Sha256Hash rootHash;
    if (!publishFile(indexPath, true, rootHash)) {
        Logger::error("Failed to publish index.html");
        return false;
    }

    WebsiteMetadata meta;
    meta.domain = WebFileStore::inferDomainFromPath(folderPath);
    meta.owner_public_key = publicKey_;
    meta.timestamp = static_cast<uint64_t>(std::time(nullptr));
    meta.root_file_hash = SHA256::toHex(rootHash);

    // deterministic built-in signature (no external crypto libs)
    const std::string material = meta.signingPayload() + "|" + meta.owner_public_key;
    meta.signature = SHA256::toHex(SHA256::hash(reinterpret_cast<const uint8_t*>(material.data()), material.size()));

    if (!mergeMetadata(meta, true)) {
        Logger::warn("Domain conflict: local metadata was not selected by deterministic rules");
    }

    Logger::info("Published decentralized website domain: " + meta.domain);
    return true;
}

std::vector<WebsiteMetadata> WebManager::listWebsites() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WebsiteMetadata> out;
    for (const auto& kv : websites_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const WebsiteMetadata& a, const WebsiteMetadata& b) {
        return a.domain < b.domain;
    });
    return out;
}

bool WebManager::openWeb(const std::string& domain,
                         const std::function<void(const Sha256Hash&, const std::string&)>& downloadByHash,
                         std::string& renderedOutput,
                         std::string& htmlOutput) {
    WebsiteMetadata meta;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = websites_.find(domain);
        if (it == websites_.end()) return false;
        meta = it->second;
    }

    auto hash = SHA256::fromHex(meta.root_file_hash);
    std::string webDir = (std::filesystem::path(storagePath_) / "websites" / domain).string();
    std::filesystem::create_directories(webDir);

    downloadByHash(hash, webDir);

    std::string htmlPath = (std::filesystem::path(webDir) / "index.html").string();
    for (int i = 0; i < 300; ++i) {
        if (std::filesystem::exists(htmlPath) && std::filesystem::file_size(htmlPath) > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!std::filesystem::exists(htmlPath)) return false;

    std::string html;
    if (!WebFileStore::readTextFile(htmlPath, html)) return false;

    htmlOutput = html;
    renderedOutput = HtmlParser::renderSimple(html);

    return true;
}

std::vector<uint8_t> WebManager::createWebsiteListPayload() const {
    auto websites = listWebsites();
    std::vector<uint8_t> out;
    writeU16(out, static_cast<uint16_t>(websites.size()));
    for (const auto& w : websites) {
        auto entry = w.serializeListEntry();
        out.insert(out.end(), entry.begin(), entry.end());
    }
    return out;
}

std::vector<std::string> WebManager::processWebsiteListPayload(const std::vector<uint8_t>& payload) {
    std::vector<std::string> needRequest;
    size_t pos = 0;
    uint16_t count = 0;
    if (!readU16(payload, pos, count)) return needRequest;

    for (uint16_t i = 0; i < count; ++i) {
        WebsiteMetadata remote;
        if (!WebsiteMetadata::deserializeListEntry(payload, pos, remote)) break;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = websites_.find(remote.domain);
        if (it == websites_.end() || isMetadataBetter(remote, it->second)) {
            needRequest.push_back(remote.domain);
        }
    }
    return needRequest;
}

std::vector<uint8_t> WebManager::createWebsiteRequestPayload(const std::string& domain) const {
    std::vector<uint8_t> out;
    writeString(out, domain);
    return out;
}

bool WebManager::parseWebsiteRequestPayload(const std::vector<uint8_t>& payload, std::string& domain) const {
    size_t pos = 0;
    return readString(payload, pos, domain) && pos == payload.size();
}

std::vector<uint8_t> WebManager::createWebsiteMetadataPayload(const std::string& domain) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = websites_.find(domain);
    if (it == websites_.end()) return {};
    return it->second.serialize();
}

bool WebManager::applyWebsiteMetadataPayload(const std::vector<uint8_t>& payload) {
    WebsiteMetadata meta;
    if (!WebsiteMetadata::deserialize(payload, meta)) return false;
    return mergeMetadata(meta, true);
}

} // namespace p2p
