#pragma once
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

inline std::string toLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Splits text into lowercase word tokens. Bytes >= 128 (UTF-8 sequences) are
// treated as word characters so accented words stay in one piece.
class Tokenizer {
public:
    static std::vector<std::string> tokenize(const std::string& text, bool dropStopWords = true) {
        std::vector<std::string> tokens;
        std::string cur;
        auto flush = [&]() {
            if (cur.empty()) return;
            if (!(dropStopWords && isStopWord(cur)) && cur.size() > 1) tokens.push_back(cur);
            cur.clear();
        };
        for (unsigned char c : text) {
            if (std::isalnum(c) || c >= 128)
                cur += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            else
                flush();
        }
        flush();
        return tokens;
    }

    static bool isStopWord(const std::string& w) {
        static const std::unordered_set<std::string> stop = {
            "the", "and", "for", "are", "but", "not", "you", "all", "any", "can", "had", "her",
            "was", "one", "our", "out", "has", "have", "this", "that", "with", "from", "they",
            "will", "would", "there", "their", "what", "about", "which", "when", "your", "into",
            "its", "is", "in", "of", "to", "an", "as", "at", "by", "it", "on", "or", "be", "we"};
        return stop.count(w) > 0;
    }
};
