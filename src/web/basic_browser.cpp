#include "basic_browser.hpp"
#include "../types.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <unordered_map>
#include <vector>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <richedit.h>

namespace p2p {

namespace {

struct BrowserCssTheme {
    bool hasBackground{false};
    bool hasTextColor{false};
    bool hasFontFamily{false};
    COLORREF background{RGB(255, 255, 255)};
    COLORREF textColor{RGB(0, 0, 0)};
    std::string fontFamily;
};

struct BrowserWindowData {
    std::string domain;
    std::string content;
    std::wstring domainW;
    std::wstring contentW;
    std::wstring addressW;
    std::wstring statusW;
    BrowserCssTheme cssTheme;
    HWND topBanner{nullptr};
    HWND titleBar{nullptr};
    HWND toolbar{nullptr};
    HWND contentView{nullptr};
    HWND statusBar{nullptr};
    HFONT titleFont{nullptr};
    HFONT uiFont{nullptr};
    HFONT contentFont{nullptr};
    HBRUSH brushTeal{nullptr};
    HBRUSH brushNavy{nullptr};
    HBRUSH brushWinGray{nullptr};
    HBRUSH brushWhite{nullptr};
    HBRUSH brushContentBg{nullptr};
    COLORREF textWhite{RGB(255, 255, 255)};
    COLORREF textBlack{RGB(0, 0, 0)};
    COLORREF contentText{RGB(0, 0, 0)};
};

static std::string trim(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static bool parseHexColor(const std::string& value, COLORREF& outColor) {
    if (value.empty() || value[0] != '#') return false;
    auto hexToByte = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    if (value.size() == 4) {
        int r = hexToByte(value[1]);
        int g = hexToByte(value[2]);
        int b = hexToByte(value[3]);
        if (r < 0 || g < 0 || b < 0) return false;
        outColor = RGB(r * 17, g * 17, b * 17);
        return true;
    }

    if (value.size() == 7) {
        int r1 = hexToByte(value[1]);
        int r2 = hexToByte(value[2]);
        int g1 = hexToByte(value[3]);
        int g2 = hexToByte(value[4]);
        int b1 = hexToByte(value[5]);
        int b2 = hexToByte(value[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
        outColor = RGB(r1 * 16 + r2, g1 * 16 + g2, b1 * 16 + b2);
        return true;
    }

    return false;
}

static bool parseColor(const std::string& rawValue, COLORREF& outColor) {
    std::string value = trim(toLower(rawValue));
    if (value.empty()) return false;

    if (parseHexColor(value, outColor)) return true;

    static const std::unordered_map<std::string, COLORREF> named = {
        {"black", RGB(0, 0, 0)},
        {"white", RGB(255, 255, 255)},
        {"red", RGB(255, 0, 0)},
        {"green", RGB(0, 128, 0)},
        {"blue", RGB(0, 0, 255)},
        {"yellow", RGB(255, 255, 0)},
        {"gray", RGB(128, 128, 128)},
        {"grey", RGB(128, 128, 128)},
        {"silver", RGB(192, 192, 192)},
        {"navy", RGB(0, 0, 128)},
        {"teal", RGB(0, 128, 128)},
        {"maroon", RGB(128, 0, 0)},
        {"purple", RGB(128, 0, 128)},
        {"lime", RGB(0, 255, 0)},
        {"aqua", RGB(0, 255, 255)},
        {"fuchsia", RGB(255, 0, 255)}
    };

    auto it = named.find(value);
    if (it != named.end()) {
        outColor = it->second;
        return true;
    }

    return false;
}

static void parseDeclarations(const std::string& declarations,
                              std::unordered_map<std::string, std::string>& out) {
    size_t start = 0;
    while (start < declarations.size()) {
        size_t end = declarations.find(';', start);
        if (end == std::string::npos) end = declarations.size();
        std::string line = declarations.substr(start, end - start);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = toLower(trim(line.substr(0, colon)));
            std::string val = trim(line.substr(colon + 1));
            if (!key.empty() && !val.empty()) {
                out[key] = val;
            }
        }
        start = end + 1;
    }
}

static std::unordered_map<std::string, std::string> extractBodyDeclarations(const std::string& html) {
    std::unordered_map<std::string, std::string> out;
    std::string lowerHtml = toLower(html);

    size_t scan = 0;
    while (true) {
        size_t styleOpen = lowerHtml.find("<style", scan);
        if (styleOpen == std::string::npos) break;
        size_t styleStart = lowerHtml.find('>', styleOpen);
        if (styleStart == std::string::npos) break;
        size_t styleClose = lowerHtml.find("</style>", styleStart + 1);
        if (styleClose == std::string::npos) break;

        std::string css = html.substr(styleStart + 1, styleClose - styleStart - 1);
        std::string lowerCss = toLower(css);

        size_t bodyPos = 0;
        while (true) {
            bodyPos = lowerCss.find("body", bodyPos);
            if (bodyPos == std::string::npos) break;
            size_t braceOpen = lowerCss.find('{', bodyPos + 4);
            if (braceOpen == std::string::npos) break;
            size_t braceClose = lowerCss.find('}', braceOpen + 1);
            if (braceClose == std::string::npos) break;

            std::string selectorTail = trim(lowerCss.substr(bodyPos + 4, braceOpen - (bodyPos + 4)));
            if (selectorTail.empty()) {
                parseDeclarations(css.substr(braceOpen + 1, braceClose - braceOpen - 1), out);
            }
            bodyPos = braceClose + 1;
        }

        scan = styleClose + 8;
    }

    size_t bodyTag = lowerHtml.find("<body");
    if (bodyTag != std::string::npos) {
        size_t bodyTagEnd = lowerHtml.find('>', bodyTag);
        if (bodyTagEnd != std::string::npos) {
            std::string bodyTagRaw = html.substr(bodyTag, bodyTagEnd - bodyTag + 1);
            std::string bodyTagLower = toLower(bodyTagRaw);
            size_t stylePos = bodyTagLower.find("style=");
            if (stylePos != std::string::npos) {
                size_t quotePos = stylePos + 6;
                if (quotePos < bodyTagRaw.size()) {
                    char quote = bodyTagRaw[quotePos];
                    if (quote == '\'' || quote == '"') {
                        size_t styleEnd = bodyTagRaw.find(quote, quotePos + 1);
                        if (styleEnd != std::string::npos) {
                            parseDeclarations(bodyTagRaw.substr(quotePos + 1, styleEnd - quotePos - 1), out);
                        }
                    }
                }
            }
        }
    }

    return out;
}

static BrowserCssTheme extractThemeFromHtml(const std::string& html) {
    BrowserCssTheme theme;
    auto declarations = extractBodyDeclarations(html);

    auto bgIt = declarations.find("background-color");
    if (bgIt == declarations.end()) bgIt = declarations.find("background");
    if (bgIt != declarations.end()) {
        COLORREF bg;
        if (parseColor(bgIt->second, bg)) {
            theme.background = bg;
            theme.hasBackground = true;
        }
    }

    auto textIt = declarations.find("color");
    if (textIt != declarations.end()) {
        COLORREF fg;
        if (parseColor(textIt->second, fg)) {
            theme.textColor = fg;
            theme.hasTextColor = true;
        }
    }

    auto fontIt = declarations.find("font-family");
    if (fontIt != declarations.end()) {
        std::string family = trim(fontIt->second);
        if (!family.empty()) {
            size_t comma = family.find(',');
            if (comma != std::string::npos) family = family.substr(0, comma);
            family = trim(family);
            if (!family.empty() && (family.front() == '"' || family.front() == '\'')) {
                family.erase(family.begin());
            }
            if (!family.empty() && (family.back() == '"' || family.back() == '\'')) {
                family.pop_back();
            }
            family = trim(family);
            if (!family.empty()) {
                theme.fontFamily = family;
                theme.hasFontFamily = true;
            }
        }
    }

    return theme;
}

static std::wstring utf8ToWide(const std::string& input) {
    if (input.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
    if (sizeNeeded <= 0) {
        sizeNeeded = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
        if (sizeNeeded <= 0) return std::wstring();
        std::wstring out(sizeNeeded, L'\0');
        MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, &out[0], sizeNeeded);
        if (!out.empty() && out.back() == L'\0') out.pop_back();
        return out;
    }
    std::wstring out(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, &out[0], sizeNeeded);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

struct TextSpan {
    LONG start{0};
    LONG end{0};
    bool bold{false};
    bool underline{false};
    int pointSize{11};
    COLORREF color{RGB(0, 0, 0)};
};

static bool startsWith(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool isWhitespaceOnly(const std::wstring& s) {
    for (wchar_t c : s) {
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') return false;
    }
    return true;
}

static std::wstring trimWide(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && std::iswspace(s[start])) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::iswspace(s[end - 1])) {
        --end;
    }
    return s.substr(start, end - start);
}

static void appendStyledLine(const std::wstring& rawLine,
                             std::wstring& outText,
                             std::vector<TextSpan>& spans,
                             COLORREF defaultColor) {
    std::wstring line = rawLine;
    int basePointSize = 11;
    bool headingBold = false;

    if (startsWith(line, L"# ")) {
        line = line.substr(2);
        basePointSize = 18;
        headingBold = true;
    } else if (startsWith(line, L"## ")) {
        line = line.substr(3);
        basePointSize = 14;
        headingBold = true;
    }

    LONG lineStart = static_cast<LONG>(outText.size());
    LONG visiblePos = lineStart;
    bool inBold = false;
    LONG boldStart = -1;
    bool inLink = false;
    LONG linkStart = -1;
    std::vector<TextSpan> inlineSpans;

    size_t i = 0;
    while (i < line.size()) {
        if (i + 1 < line.size() && line[i] == L'*' && line[i + 1] == L'*') {
            if (!inBold) {
                inBold = true;
                boldStart = visiblePos;
            } else {
                inBold = false;
                if (boldStart >= 0 && visiblePos > boldStart) {
                    TextSpan b;
                    b.start = boldStart;
                    b.end = visiblePos;
                    b.bold = true;
                    b.pointSize = basePointSize;
                    b.color = defaultColor;
                    inlineSpans.push_back(b);
                }
                boldStart = -1;
            }
            i += 2;
            continue;
        }

        if (i + 7 < line.size() && line.compare(i, 8, L"[[/LINK]") == 0 && i + 8 < line.size() && line[i + 8] == L']') {
            if (inLink) {
                if (linkStart >= 0 && visiblePos > linkStart) {
                    TextSpan link;
                    link.start = linkStart;
                    link.end = visiblePos;
                    link.underline = true;
                    link.pointSize = basePointSize;
                    link.color = RGB(0, 0, 200);
                    inlineSpans.push_back(link);
                }
                inLink = false;
                linkStart = -1;
            }
            i += 9;
            continue;
        }

        if (i + 6 < line.size() && line.compare(i, 7, L"[[LINK:") == 0) {
            size_t close = line.find(L"]]", i + 7);
            if (close != std::wstring::npos) {
                inLink = true;
                linkStart = visiblePos;
                i = close + 2;
                continue;
            }
        }

        outText.push_back(line[i]);
        ++visiblePos;
        ++i;
    }

    if (inBold && boldStart >= 0 && visiblePos > boldStart) {
        TextSpan b;
        b.start = boldStart;
        b.end = visiblePos;
        b.bold = true;
        b.pointSize = basePointSize;
        b.color = defaultColor;
        inlineSpans.push_back(b);
    }

    if (inLink && linkStart >= 0 && visiblePos > linkStart) {
        TextSpan link;
        link.start = linkStart;
        link.end = visiblePos;
        link.underline = true;
        link.pointSize = basePointSize;
        link.color = RGB(0, 0, 200);
        inlineSpans.push_back(link);
    }

    LONG lineEnd = visiblePos;
    std::wstring visibleLine = outText.substr(lineStart, lineEnd - lineStart);
    if (lineEnd > lineStart && !isWhitespaceOnly(visibleLine)) {
        TextSpan base;
        base.start = lineStart;
        base.end = lineEnd;
        base.bold = headingBold;
        base.pointSize = basePointSize;
        base.color = defaultColor;
        spans.push_back(base);

        for (const auto& s : inlineSpans) {
            spans.push_back(s);
        }
    }

    outText += L"\r\n";
}

static void applySpan(HWND richEdit, const TextSpan& span, const BrowserWindowData* data) {
    if (!data || span.end <= span.start) return;

    CHARRANGE range{};
    range.cpMin = span.start;
    range.cpMax = span.end;
    SendMessage(richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));

    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_BOLD | CFM_SIZE | CFM_COLOR | CFM_UNDERLINE | CFM_FACE;
    fmt.dwEffects = 0;
    if (span.bold) fmt.dwEffects |= CFE_BOLD;
    if (span.underline) fmt.dwEffects |= CFE_UNDERLINE;
    fmt.yHeight = span.pointSize * 20;
    fmt.crTextColor = span.color;

    if (data->cssTheme.hasFontFamily) {
        std::wstring ff = utf8ToWide(data->cssTheme.fontFamily);
        if (!ff.empty()) {
            wcsncpy_s(fmt.szFaceName, ff.c_str(), LF_FACESIZE - 1);
        }
    }

    SendMessage(richEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
}

static void setRichStyledContent(HWND richEdit, BrowserWindowData* data) {
    if (!richEdit || !data) return;

    std::wstring normalized = data->contentW;
    std::replace(normalized.begin(), normalized.end(), L'\r', L'\n');

    std::wstring out;
    std::vector<TextSpan> spans;
    out.reserve(normalized.size() + 64);
    bool previousBlankLine = false;

    size_t start = 0;
    while (start <= normalized.size()) {
        size_t end = normalized.find(L'\n', start);
        if (end == std::wstring::npos) end = normalized.size();
        std::wstring line = trimWide(normalized.substr(start, end - start));
        if (!line.empty()) {
            appendStyledLine(line, out, spans, data->contentText);
            previousBlankLine = false;
        } else {
            if (!previousBlankLine) {
                out += L"\r\n";
                previousBlankLine = true;
            }
        }
        if (end == normalized.size()) break;
        start = end + 1;
    }

    SetWindowTextW(richEdit, out.c_str());
    SendMessage(richEdit, EM_SETBKGNDCOLOR, 0,
                static_cast<LPARAM>(data->cssTheme.hasBackground ? data->cssTheme.background : RGB(255, 255, 255)));

    for (const auto& span : spans) {
        applySpan(richEdit, span, data);
    }

    CHARRANGE reset{};
    reset.cpMin = -1;
    reset.cpMax = -1;
    SendMessage(richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&reset));
}

} // namespace

static LRESULT CALLBACK BrowserWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<BrowserWindowData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            auto* initData = reinterpret_cast<BrowserWindowData*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initData));
            data = initData;

            data->brushTeal = CreateSolidBrush(RGB(0, 128, 128));
            data->brushNavy = CreateSolidBrush(RGB(0, 0, 128));
            data->brushWinGray = CreateSolidBrush(RGB(212, 208, 200));
            data->brushWhite = CreateSolidBrush(RGB(255, 255, 255));
            data->brushContentBg = data->brushWhite;

            if (data->cssTheme.hasBackground) {
                data->brushContentBg = CreateSolidBrush(data->cssTheme.background);
            }
            if (data->cssTheme.hasTextColor) {
                data->contentText = data->cssTheme.textColor;
            }

            data->domainW = utf8ToWide(data->domain);
            data->contentW = utf8ToWide(data->content);
            data->addressW = utf8ToWide("Address: p2p://" + data->domain);
            data->statusW = utf8ToWide((data->cssTheme.hasBackground || data->cssTheme.hasTextColor || data->cssTheme.hasFontFamily)
                ? "Rendered from decentralized network - CSS body style applied"
                : "Rendered from decentralized network - Owner key verified");

            data->titleFont = CreateFontW(
                -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Verdana");
            data->uiFont = CreateFontW(
                -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Verdana");
            data->contentFont = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN, L"Consolas");
            if (data->cssTheme.hasFontFamily) {
                HFONT cssFont = CreateFontW(
                    -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, utf8ToWide(data->cssTheme.fontFamily).c_str());
                if (cssFont) {
                    DeleteObject(data->contentFont);
                    data->contentFont = cssFont;
                }
            }

            data->topBanner = CreateWindowExW(
                0, L"STATIC", L"P2P Decentralized Web Browser",
                WS_CHILD | WS_VISIBLE,
                0, 0, 100, 30,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            SendMessage(data->topBanner, WM_SETFONT, reinterpret_cast<WPARAM>(data->titleFont), TRUE);

            data->titleBar = CreateWindowExW(
                0, L"STATIC", data->domainW.c_str(),
                WS_CHILD | WS_VISIBLE,
                0, 0, 100, 26,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            SendMessage(data->titleBar, WM_SETFONT, reinterpret_cast<WPARAM>(data->titleFont), TRUE);

            data->toolbar = CreateWindowExW(
                0, L"STATIC", data->addressW.c_str(),
                WS_CHILD | WS_VISIBLE,
                0, 0, 100, 24,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            SendMessage(data->toolbar, WM_SETFONT, reinterpret_cast<WPARAM>(data->uiFont), TRUE);

            data->contentView = CreateWindowExW(
                WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                0, 0, 100, 100,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            SendMessage(data->contentView, WM_SETFONT, reinterpret_cast<WPARAM>(data->contentFont), TRUE);
            setRichStyledContent(data->contentView, data);

            data->statusBar = CreateWindowExW(
                0, L"STATIC", data->statusW.c_str(),
                WS_CHILD | WS_VISIBLE,
                0, 0, 100, 24,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            SendMessage(data->statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(data->uiFont), TRUE);
            break;
        }

        case WM_SIZE: {
            if (data) {
                int w = LOWORD(lParam);
                int h = HIWORD(lParam);

                const int outer = 14;
                const int topBannerH = 34;
                const int innerPad = 10;
                const int titleH = 26;
                const int toolbarH = 24;
                const int statusH = 24;
                const int framePad = 8;

                int panelX = outer;
                int panelY = topBannerH + outer;
                int panelW = w - outer * 2;
                int panelH = h - panelY - outer;

                if (data->topBanner) MoveWindow(data->topBanner, 0, 0, w, topBannerH, TRUE);
                if (data->titleBar) MoveWindow(data->titleBar, panelX + 2, panelY + 2, panelW - 4, titleH, TRUE);
                if (data->toolbar) MoveWindow(data->toolbar, panelX + 2, panelY + 2 + titleH, panelW - 4, toolbarH, TRUE);

                const int contentX = panelX + innerPad;
                const int contentY = panelY + 2 + titleH + toolbarH + framePad;
                const int contentW = panelW - innerPad * 2;
                const int statusY = panelY + panelH - statusH - 2;
                const int contentHRaw = statusY - contentY - framePad;
                const int contentH = (contentHRaw > 80) ? contentHRaw : 80;

                if (data->contentView) MoveWindow(data->contentView, contentX, contentY, contentW, contentH, TRUE);
                if (data->statusBar) MoveWindow(data->statusBar, panelX + 2, statusY, panelW - 4, statusH, TRUE);
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            if (data && data->brushTeal) {
                FillRect(hdc, &client, data->brushTeal);

                const int outer = 14;
                const int topBannerH = 34;
                int panelX = outer;
                int panelY = topBannerH + outer;
                int panelW = client.right - outer * 2;
                int panelH = client.bottom - panelY - outer;

                RECT panel{panelX, panelY, panelX + panelW, panelY + panelH};
                FillRect(hdc, &panel, data->brushWinGray);

                HPEN darkPen = CreatePen(PS_SOLID, 2, RGB(80, 80, 80));
                HPEN lightPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, darkPen));

                MoveToEx(hdc, panel.left, panel.bottom - 1, nullptr);
                LineTo(hdc, panel.right - 1, panel.bottom - 1);
                LineTo(hdc, panel.right - 1, panel.top);

                SelectObject(hdc, lightPen);
                MoveToEx(hdc, panel.left, panel.bottom - 1, nullptr);
                LineTo(hdc, panel.left, panel.top);
                LineTo(hdc, panel.right - 1, panel.top);

                SelectObject(hdc, oldPen);
                DeleteObject(darkPen);
                DeleteObject(lightPen);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            if (!data) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND ctrl = reinterpret_cast<HWND>(lParam);

            if (ctrl == data->topBanner || ctrl == data->titleBar) {
                SetTextColor(hdc, data->textWhite);
                SetBkColor(hdc, RGB(0, 0, 128));
                return reinterpret_cast<LRESULT>(data->brushNavy);
            }
            if (ctrl == data->toolbar || ctrl == data->statusBar) {
                SetTextColor(hdc, data->textBlack);
                SetBkColor(hdc, RGB(212, 208, 200));
                return reinterpret_cast<LRESULT>(data->brushWinGray);
            }
            break;
        }

        case WM_DESTROY:
            if (data) {
                if (data->titleFont) DeleteObject(data->titleFont);
                if (data->uiFont) DeleteObject(data->uiFont);
                if (data->contentFont) DeleteObject(data->contentFont);
                if (data->brushTeal) DeleteObject(data->brushTeal);
                if (data->brushNavy) DeleteObject(data->brushNavy);
                if (data->brushWinGray) DeleteObject(data->brushWinGray);
                if (data->brushContentBg && data->brushContentBg != data->brushWhite) DeleteObject(data->brushContentBg);
                if (data->brushWhite) DeleteObject(data->brushWhite);
                delete data;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void runBrowserWindow(const std::string& domain,
                             const std::string& renderedContent,
                             const std::string& htmlContent) {
    static std::atomic<bool> richEditLoaded{false};
    if (!richEditLoaded.exchange(true)) {
        LoadLibraryW(L"Msftedit.dll");
    }

    static std::atomic<bool> classRegistered{false};
    if (!classRegistered.exchange(true)) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = BrowserWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"P2PBasicBrowserWnd";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
    }

    auto* data = new BrowserWindowData();
    data->domain = domain;
    data->content = renderedContent;
    data->cssTheme = extractThemeFromHtml(htmlContent);

    std::wstring title = utf8ToWide("P2P Basic Browser - " + domain);
    HWND hwnd = CreateWindowExW(
        0,
        L"P2PBasicBrowserWnd",
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        nullptr, nullptr, GetModuleHandle(nullptr), data);

    if (!hwnd) {
        delete data;
        return;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool BasicBrowser::open(const std::string& domain, const std::string& renderedContent, const std::string& htmlContent) {
    std::thread([domain, renderedContent, htmlContent]() {
        runBrowserWindow(domain, renderedContent, htmlContent);
    }).detach();
    return true;
}

} // namespace p2p

#else

namespace p2p {

bool BasicBrowser::open(const std::string& domain, const std::string& renderedContent, const std::string& htmlContent) {
    Logger::warn("BasicBrowser window mode is currently implemented for Windows only");
    (void)domain;
    (void)renderedContent;
    (void)htmlContent;
    return false;
}

} // namespace p2p

#endif
