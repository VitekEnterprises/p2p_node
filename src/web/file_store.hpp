#pragma once

#include <string>
#include <vector>

namespace p2p {

class WebFileStore {
public:
    static bool findIndexHtml(const std::string& folderPath, std::string& outPath);
    static std::string inferDomainFromPath(const std::string& folderPath);
    static bool readTextFile(const std::string& path, std::string& out);
};

} // namespace p2p
