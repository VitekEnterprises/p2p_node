#pragma once

#include "website_metadata.hpp"
#include "file_store.hpp"
#include "html_parser.hpp"
#include "../types.hpp"
#include <functional>
#include <mutex>
#include <unordered_map>

namespace p2p {

class WebManager {
public:
    explicit WebManager(const std::string& storagePath);

    bool init();

    bool makeWeb(const std::string& folderPath,
                 const std::function<bool(const std::string&, bool, Sha256Hash&)>& publishFile);

    std::vector<WebsiteMetadata> listWebsites() const;

    bool openWeb(const std::string& domain,
                 const std::function<void(const Sha256Hash&, const std::string&)>& downloadByHash,
                 std::string& renderedOutput,
                 std::string& htmlOutput);

    std::vector<uint8_t> createWebsiteListPayload() const;
    std::vector<std::string> processWebsiteListPayload(const std::vector<uint8_t>& payload);

    std::vector<uint8_t> createWebsiteRequestPayload(const std::string& domain) const;
    bool parseWebsiteRequestPayload(const std::vector<uint8_t>& payload, std::string& domain) const;

    std::vector<uint8_t> createWebsiteMetadataPayload(const std::string& domain) const;
    bool applyWebsiteMetadataPayload(const std::vector<uint8_t>& payload);

    const std::string& publicKey() const { return publicKey_; }

private:
    std::string storagePath_;
    std::string identityPath_;
    std::string websitesPath_;

    std::string privateKey_;
    std::string publicKey_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, WebsiteMetadata> websites_;

    bool loadIdentity();
    bool saveIdentity() const;

    void loadWebsites();
    void saveWebsites() const;

    bool mergeMetadata(const WebsiteMetadata& incoming, bool persist);
};

} // namespace p2p
