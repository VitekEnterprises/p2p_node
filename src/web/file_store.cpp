#include "file_store.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace p2p {

bool WebFileStore::findIndexHtml(const std::string& folderPath, std::string& outPath) {
    std::filesystem::path p(folderPath);
    auto idx = p / "index.html";
    if (std::filesystem::exists(idx) && std::filesystem::is_regular_file(idx)) {
        outPath = idx.string();
        return true;
    }
    return false;
}

std::string WebFileStore::inferDomainFromPath(const std::string& folderPath) {
    std::filesystem::path p(folderPath);
    if (p.filename().empty()) return "site.local";
    std::string d = p.filename().string();
    for (char& c : d) {
        if (c == ' ') c = '-';
    }
    return d;
}

bool WebFileStore::readTextFile(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

} // namespace p2p
