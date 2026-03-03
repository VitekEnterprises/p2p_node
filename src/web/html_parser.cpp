#include "html_parser.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace p2p {

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string normalizeWhitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool prevSpace = false;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!prevSpace) {
                out.push_back(' ');
                prevSpace = true;
            }
        } else {
            out.push_back(static_cast<char>(ch));
            prevSpace = false;
        }
    }
    return out;
}

static bool tagIs(const std::string& tag, const std::string& name) {
    if (tag == name) return true;
    if (tag.size() > name.size() && tag.compare(0, name.size(), name) == 0 && std::isspace(static_cast<unsigned char>(tag[name.size()]))) {
        return true;
    }
    return false;
}

static std::string decodeHtmlEntities(const std::string& text) {
    static const std::unordered_map<std::string, std::string> entities = {
        {"amp", "&"},
        {"lt", "<"},
        {"gt", ">"},
        {"quot", "\""},
        {"apos", "'"},
        {"nbsp", " "},
        {"copy", "(c)"},
        {"reg", "(R)"},
        {"trade", "TM"},
        {"euro", "EUR"}
    };

    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }

        size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(text[i]);
            continue;
        }

        std::string key = text.substr(i + 1, semi - i - 1);
        if (!key.empty() && key[0] == '#') {
            int base = 10;
            size_t pos = 1;
            if (key.size() > 2 && (key[1] == 'x' || key[1] == 'X')) {
                base = 16;
                pos = 2;
            }
            unsigned long code = 0;
            try {
                code = std::stoul(key.substr(pos), nullptr, base);
            } catch (...) {
                code = 0;
            }
            if (code > 0 && code <= 0x7F) {
                out.push_back(static_cast<char>(code));
                i = semi;
                continue;
            }
        } else {
            auto it = entities.find(toLower(key));
            if (it != entities.end()) {
                out += it->second;
                i = semi;
                continue;
            }
        }

        out.push_back(text[i]);
    }

    return out;
}

std::string HtmlParser::renderSimple(const std::string& html) {
    std::ostringstream out;
    const std::string lowerHtml = toLower(html);

    size_t i = 0;
    while (i < html.size()) {
        if (lowerHtml.compare(i, 4, "<!--") == 0) {
            size_t endComment = lowerHtml.find("-->", i + 4);
            if (endComment == std::string::npos) break;
            i = endComment + 3;
            continue;
        }

        if (html[i] != '<') {
            size_t nextTag = html.find('<', i);
            if (nextTag == std::string::npos) nextTag = html.size();
                std::string chunk = normalizeWhitespace(decodeHtmlEntities(html.substr(i, nextTag - i)));
                if (!chunk.empty() && chunk != " ") {
                    out << chunk;
                }
            i = nextTag;
            continue;
        }

        size_t close = html.find('>', i + 1);
        if (close == std::string::npos) break;

        std::string rawTag = html.substr(i + 1, close - i - 1);
        std::string tag = toLower(rawTag);

        if (tagIs(tag, "head")) {
            size_t endHead = lowerHtml.find("</head>", close + 1);
            if (endHead == std::string::npos) break;
            i = endHead + 7;
            continue;
        }

        if (tagIs(tag, "style")) {
            size_t endStyle = lowerHtml.find("</style>", close + 1);
            if (endStyle == std::string::npos) break;
            i = endStyle + 8;
            continue;
        }

        if (tagIs(tag, "script")) {
            size_t endScript = lowerHtml.find("</script>", close + 1);
            if (endScript == std::string::npos) break;
            i = endScript + 9;
            continue;
        }

        if (tag == "br" || tag == "br/" || tag == "/p" || tag == "/h1" || tag == "/h2" || tag == "/h3" ||
            tag == "/div" || tag == "/section" || tag == "/ul" || tag == "/ol" || tag == "hr") {
            out << "\n";
        } else if (tag == "h1") {
            out << "\n# ";
        } else if (tag == "h2") {
            out << "\n## ";
        } else if (tag == "h3") {
            out << "\n### ";
        } else if (tag == "li") {
            out << "\n• ";
        } else if (tag == "/li") {
            out << "\n";
        } else if (tag == "strong" || tag == "b") {
            out << "**";
        } else if (tag == "/strong" || tag == "/b") {
            out << "**";
        } else if (tag.rfind("a ", 0) == 0) {
            auto hrefPos = tag.find("href=");
            if (hrefPos != std::string::npos) {
                size_t q1 = rawTag.find('"', hrefPos);
                size_t q2 = (q1 == std::string::npos) ? std::string::npos : rawTag.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    out << "[[LINK:" << rawTag.substr(q1 + 1, q2 - q1 - 1) << "]]";
                }
            }
        } else if (tag == "/a") {
            out << "[[/LINK]]";
        } else if (tag.rfind("img ", 0) == 0) {
            auto srcPos = tag.find("src=");
            if (srcPos != std::string::npos) {
                size_t q1 = rawTag.find('"', srcPos);
                size_t q2 = (q1 == std::string::npos) ? std::string::npos : rawTag.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    out << " [IMG:" << rawTag.substr(q1 + 1, q2 - q1 - 1) << "] ";
                }
            }
        }

        i = close + 1;
    }

    return out.str();
}

} // namespace p2p
