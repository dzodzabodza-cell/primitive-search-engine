#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "document.hpp"
#include "index.hpp"
#include "sources.hpp"
#include "tokenizer.hpp"

struct Query {
    std::string text;       // free-text keywords (may be empty when only filters are used)
    std::string category;   // empty = any (case-insensitive match)
    int from = 0;           // YYYYMMDD, 0 = no lower bound
    int to = 0;             // YYYYMMDD, 0 = no upper bound
    bool matchAll = true;   // true: AND semantics, false: OR
    size_t limit = 10;
};

struct Result {
    const Document* doc;
    double score;
    std::string snippet;
};

class SearchEngine {
public:
    // Loads a source immediately, indexes its documents and remembers it for reindex().
    // Returns the number of documents added.
    size_t addSource(std::unique_ptr<DataSource> src) {
        std::vector<Document> loaded = src->load();
        for (Document& d : loaded) index(std::move(d));
        sources_.push_back(std::move(src));
        return loaded.size();
    }

    // Drops everything and reloads all registered sources.
    size_t reindex() {
        docs_.clear();
        index_.clear();
        size_t total = 0;
        for (auto& s : sources_) {
            try {
                std::vector<Document> loaded = s->load();
                total += loaded.size();
                for (Document& d : loaded) index(std::move(d));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "warning: %s: %s\n", s->name().c_str(), e.what());
            }
        }
        return total;
    }

    std::vector<Result> search(const Query& q) const {
        const std::vector<std::string> terms = Tokenizer::tokenize(q.text);
        const std::string wantCat = toLower(q.category);
        auto accept = [&](int id) { return matches(docs_[id], wantCat, q); };

        std::vector<std::pair<int, double>> hits;
        if (terms.empty()) {
            // Filter-only query (browse): newest documents first.
            for (const Document& d : docs_)
                if (accept(d.id)) hits.emplace_back(d.id, 0.0);
            std::sort(hits.begin(), hits.end(), [&](const auto& a, const auto& b) {
                const int da = docs_[a.first].date, db = docs_[b.first].date;
                return da != db ? da > db : a.first < b.first;
            });
        } else {
            hits = index_.search(terms, q.matchAll, accept);
        }

        std::vector<Result> out;
        for (const auto& [id, score] : hits) {
            if (out.size() >= q.limit) break;
            out.push_back({&docs_[id], score, makeSnippet(docs_[id], terms)});
        }
        return out;
    }

    // Number of matches ignoring the display limit.
    size_t countMatches(Query q) const {
        q.limit = static_cast<size_t>(-1);
        return search(q).size();
    }

    const Document* get(int id) const {
        return (id >= 0 && static_cast<size_t>(id) < docs_.size()) ? &docs_[id] : nullptr;
    }

    std::map<std::string, size_t> categories() const {
        std::map<std::string, size_t> counts;
        for (const Document& d : docs_) ++counts[d.category];
        return counts;
    }

    size_t docCount() const { return docs_.size(); }
    size_t termCount() const { return index_.termCount(); }
    size_t sourceCount() const { return sources_.size(); }

private:
    void index(Document d) {
        d.id = static_cast<int>(docs_.size());
        docs_.push_back(std::move(d));
        index_.add(docs_.back());
    }

    static bool matches(const Document& d, const std::string& wantCat, const Query& q) {
        if (!wantCat.empty() && toLower(d.category) != wantCat) return false;
        if (q.from && (d.date == 0 || d.date < q.from)) return false;
        if (q.to && (d.date == 0 || d.date > q.to)) return false;
        return true;
    }

    static std::string makeSnippet(const Document& d, const std::vector<std::string>& terms) {
        // Collapse whitespace so snippets stay on one line.
        std::string flat;
        bool space = false;
        for (unsigned char c : d.body) {
            if (std::isspace(c)) { space = true; continue; }
            if (space && !flat.empty()) flat += ' ';
            space = false;
            flat += static_cast<char>(c);
        }
        if (flat.empty()) return "";

        const std::string lower = toLower(flat);
        size_t pos = std::string::npos;
        for (const auto& t : terms) pos = std::min(pos, lower.find(t));
        size_t start = (pos == std::string::npos || pos < 60) ? 0 : pos - 60;
        if (start > 0) {  // begin at a word boundary
            size_t sp = flat.find(' ', start);
            if (sp != std::string::npos && sp - start < 20) start = sp + 1;
        }
        size_t end = std::min(flat.size(), start + 180);
        while (end < flat.size() && (static_cast<unsigned char>(flat[end]) & 0xC0) == 0x80) --end;

        std::string s = flat.substr(start, end - start);
        if (start > 0) s = "..." + s;
        if (end < flat.size()) s += "...";
        return s;
    }

    std::vector<Document> docs_;
    InvertedIndex index_;
    std::vector<std::unique_ptr<DataSource>> sources_;
};
