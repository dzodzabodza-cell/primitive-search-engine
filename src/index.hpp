#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "document.hpp"
#include "tokenizer.hpp"

// One entry of a posting list: which document contains the term and how often.
struct Posting {
    int docId;
    int tf;
};

// Classic inverted index: term -> list of postings, ranked with TF-IDF.
class InvertedIndex {
public:
    static constexpr int kTitleWeight = 3;  // a title hit counts like 3 body hits

    void clear() {
        postings_.clear();
        docLen_.clear();
    }

    void add(const Document& doc) {
        std::unordered_map<std::string, int> counts;
        for (const auto& t : Tokenizer::tokenize(doc.title)) counts[t] += kTitleWeight;
        for (const auto& t : Tokenizer::tokenize(doc.body)) counts[t] += 1;
        int len = 0;
        for (const auto& [term, tf] : counts) {
            postings_[term].push_back({doc.id, tf});
            len += tf;
        }
        docLen_[doc.id] = len;
    }

    size_t docCount() const { return docLen_.size(); }
    size_t termCount() const { return postings_.size(); }

    // Returns (docId, score) pairs sorted by descending score.
    // matchAll: every term must appear (AND); otherwise any term is enough (OR).
    // accept: extra filter (category / date range) applied to each candidate.
    std::vector<std::pair<int, double>> search(const std::vector<std::string>& terms, bool matchAll,
                                               const std::function<bool(int)>& accept) const {
        std::unordered_set<std::string> unique(terms.begin(), terms.end());
        std::unordered_map<int, double> scores;
        std::unordered_map<int, size_t> hits;
        const double n = static_cast<double>(docLen_.size());

        for (const auto& term : unique) {
            auto it = postings_.find(term);
            if (it == postings_.end()) continue;
            const double idf = std::log(1.0 + n / static_cast<double>(it->second.size()));
            for (const Posting& p : it->second) {
                if (accept && !accept(p.docId)) continue;
                scores[p.docId] += (1.0 + std::log(static_cast<double>(p.tf))) * idf;
                ++hits[p.docId];
            }
        }

        std::vector<std::pair<int, double>> out;
        for (const auto& [id, score] : scores) {
            if (matchAll && hits[id] != unique.size()) continue;
            const double len = static_cast<double>(docLen_.at(id));
            out.emplace_back(id, score / (1.0 + 0.2 * std::log(1.0 + len)));
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        return out;
    }

private:
    std::unordered_map<std::string, std::vector<Posting>> postings_;
    std::unordered_map<int, int> docLen_;
};
