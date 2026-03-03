#pragma once

#include <string>

namespace p2p {

class BasicBrowser {
public:
    static bool open(const std::string& domain, const std::string& renderedContent, const std::string& htmlContent);
};

} // namespace p2p
