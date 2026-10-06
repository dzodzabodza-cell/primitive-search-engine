#pragma once
#include <cstdio>
#include <string>

// A single indexed record. Dates are stored as YYYYMMDD integers (0 = unknown)
// so that range filtering is a plain integer comparison.
struct Document {
    int id = 0;
    std::string title;
    std::string body;
    std::string category;
    std::string source;  // where the document came from (file path, URL...)
    int date = 0;
};

inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Parses "YYYY-MM-DD" into YYYYMMDD. Returns 0 when the text is not a valid date.
inline int parseDate(const std::string& text) {
    std::string s = trim(text);
    int y = 0, m = 0, d = 0;
    char extra = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d%c", &y, &m, &d, &extra) != 3) return 0;
    if (y < 1000 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return y * 10000 + m * 100 + d;
}

inline std::string formatDate(int date) {
    if (date == 0) return "unknown date";
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", date / 10000, (date / 100) % 100, date % 100);
    return buf;
}
