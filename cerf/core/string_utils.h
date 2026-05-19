#pragma once
#include <windows.h>
#include <cctype>
#include <string>

inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 0) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    return s;
}

inline std::string GetCerfDir() {
    wchar_t p[MAX_PATH];
    ::GetModuleFileNameW(NULL, p, MAX_PATH);
    std::wstring ws(p);
    size_t sep = ws.find_last_of(L"\\/");
    if (sep == std::wstring::npos) return "";
    return WideToUtf8(ws.substr(0, sep + 1));
}

inline std::wstring Utf8ToWide(const char* u) {
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, -1, &w[0], n);
    return w;
}

inline void ToUpperAscii(std::string& s) {
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}
