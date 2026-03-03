#pragma once

#include <string>

namespace p2p {

class HtmlParser {
public:
    static std::string renderSimple(const std::string& html);
};

} // namespace p2p
