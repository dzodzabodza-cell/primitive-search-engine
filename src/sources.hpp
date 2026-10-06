#pragma once
#include <sys/stat.h>

#include <algorithm>
#include <cctype>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "document.hpp"
#include "tokenizer.hpp"

// Abstract data source. Concrete sources only produce Documents; the engine
// takes care of ids and indexing.
class DataSource {
public:
    virtual ~DataSource() = default;
    virtual std::string name() const = 0;
    virtual std::vector<Document> load() = 0;
};

inline std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------- CSV (structured data) ----------

inline std::vector<std::vector<std::string>> parseCsv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            row.push_back(field);
            field.clear();
        } else if (c == '\n') {
            row.push_back(field);
            field.clear();
            if (!(row.size() == 1 && trim(row[0]).empty())) rows.push_back(row);
            row.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

// Expects a header row naming the columns: title, body (or content/text),
// category, date (YYYY-MM-DD). Column order does not matter.
inline std::vector<Document> documentsFromCsv(std::string text, const std::string& source) {
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // BOM
    auto rows = parseCsv(text);
    if (rows.empty()) return {};

    std::unordered_map<std::string, size_t> col;
    for (size_t i = 0; i < rows[0].size(); ++i) col[toLower(trim(rows[0][i]))] = i;
    auto find = [&](std::initializer_list<const char*> names) -> int {
        for (const char* n : names) {
            auto it = col.find(n);
            if (it != col.end()) return static_cast<int>(it->second);
        }
        return -1;
    };
    const int cTitle = find({"title", "name", "headline"});
    const int cBody = find({"body", "content", "text", "description"});
    const int cCat = find({"category", "topic", "type"});
    const int cDate = find({"date", "published"});
    if (cTitle < 0 && cBody < 0)
        throw std::runtime_error("CSV needs at least a 'title' or 'body' column: " + source);

    auto cell = [&](const std::vector<std::string>& r, int c) {
        return (c >= 0 && static_cast<size_t>(c) < r.size()) ? trim(r[c]) : std::string();
    };
    std::vector<Document> docs;
    for (size_t i = 1; i < rows.size(); ++i) {
        Document d;
        d.title = cell(rows[i], cTitle);
        d.body = cell(rows[i], cBody);
        d.category = cell(rows[i], cCat);
        if (d.category.empty()) d.category = "uncategorized";
        d.date = parseDate(cell(rows[i], cDate));
        d.source = source + ":" + std::to_string(i + 1);
        if (d.title.empty() && d.body.empty()) continue;
        docs.push_back(std::move(d));
    }
    return docs;
}

class CsvSource : public DataSource {
public:
    explicit CsvSource(std::string path) : path_(std::move(path)) {}
    std::string name() const override { return "csv:" + path_; }
    std::vector<Document> load() override { return documentsFromCsv(readFile(path_), path_); }

private:
    std::string path_;
};

// ---------- Directory of .txt / .md files (unstructured data) ----------
//
// A file may start with optional header lines:
//   Title: ...
//   Category: ...
//   Date: YYYY-MM-DD
// followed by a blank line and the body. Without them the title is the file
// name, the category is the sub-folder name and the date is the modification time.

class DirectorySource : public DataSource {
public:
    explicit DirectorySource(std::string root) : root_(std::move(root)) {}
    std::string name() const override { return "dir:" + root_; }

    std::vector<Document> load() override {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(root_, ec)) throw std::runtime_error("not a directory: " + root_);

        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(root_, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = toLower(it->path().extension().string());
            if (ext == ".txt" || ext == ".md") files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());

        std::vector<Document> docs;
        for (const auto& p : files) {
            try {
                docs.push_back(parseFile(p));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "warning: skipped %s (%s)\n", p.string().c_str(), e.what());
            }
        }
        return docs;
    }

private:
    Document parseFile(const std::filesystem::path& path) const {
        namespace fs = std::filesystem;
        Document d;
        d.source = path.string();
        d.title = path.stem().string();

        fs::path rel = fs::relative(path, root_);
        d.category = rel.has_parent_path() ? rel.begin()->string() : "uncategorized";

        struct stat st {};
        if (stat(path.c_str(), &st) == 0) {
            std::tm tm{};
            localtime_r(&st.st_mtime, &tm);
            d.date = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
        }

        std::istringstream in(readFile(path.string()));
        std::string line, body;
        bool header = true, titleFromHeader = false, firstBodyLine = true;
        while (std::getline(in, line)) {
            if (header) {
                auto colon = line.find(':');
                std::string key = colon == std::string::npos ? "" : toLower(trim(line.substr(0, colon)));
                std::string val = colon == std::string::npos ? "" : trim(line.substr(colon + 1));
                if (key == "title" && !val.empty()) { d.title = val; titleFromHeader = true; continue; }
                if (key == "category" && !val.empty()) { d.category = val; continue; }
                if (key == "date") {
                    if (int dt = parseDate(val)) d.date = dt;
                    continue;
                }
                header = false;
                if (trim(line).empty()) continue;  // blank separator after the header
            }
            if (firstBodyLine && !titleFromHeader && !trim(line).empty() && line.size() <= 120) {
                d.title = trim(line);  // no Title header: first line acts as the title
                firstBodyLine = false;
                continue;
            }
            firstBodyLine = false;
            body += line + "\n";
        }
        d.body = body;
        return d;
    }

    std::string root_;
};

// ---------- Remote HTTP(S) source ----------
//
// Uses the system `curl` binary so the project has no library dependencies.
// URLs ending in .csv are parsed as structured data; anything else is treated
// as a single unstructured document (HTML tags are stripped).

class RemoteSource : public DataSource {
public:
    explicit RemoteSource(std::string url) : url_(std::move(url)) {}
    std::string name() const override { return "url:" + url_; }

    std::vector<Document> load() override {
        validate();
        const std::string raw = fetch();
        std::string path = url_.substr(0, url_.find_first_of("?#"));
        if (toLower(path).size() >= 4 && toLower(path).compare(path.size() - 4, 4, ".csv") == 0)
            return documentsFromCsv(raw, url_);

        Document d;
        d.source = url_;
        d.category = "remote";
        d.title = extractTitle(raw);
        if (d.title.empty()) d.title = url_;
        d.body = stripTags(raw);
        return {d};
    }

private:
    void validate() const {
        const bool schemeOk = url_.rfind("http://", 0) == 0 || url_.rfind("https://", 0) == 0;
        if (!schemeOk) throw std::runtime_error("only http:// and https:// URLs are supported");
        for (unsigned char c : url_) {
            const bool ok = std::isalnum(c) || std::string("-._~:/?#[]@!$&()*+,;=%").find(c) != std::string::npos;
            if (!ok) throw std::runtime_error("URL contains unsupported characters");
        }
    }

    std::string fetch() const {
        // The URL was validated above (no quotes, spaces or backslashes), so it is
        // safe inside single quotes.
        const std::string cmd = "curl -fsSL --max-time 20 '" + url_ + "' 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) throw std::runtime_error("cannot run curl");
        std::string data;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, pipe)) > 0) data.append(buf, n);
        const int status = pclose(pipe);
        if (status != 0 || data.empty()) throw std::runtime_error("download failed: " + url_);
        return data;
    }

    static std::string extractTitle(const std::string& html) {
        const std::string lower = toLower(html);
        size_t a = lower.find("<title>");
        if (a == std::string::npos) return "";
        a += 7;
        size_t b = lower.find("</title>", a);
        if (b == std::string::npos) return "";
        return trim(html.substr(a, b - a));
    }

    static std::string stripTags(const std::string& html) {
        std::string out;
        bool inTag = false;
        for (char c : html) {
            if (c == '<') inTag = true;
            else if (c == '>') { inTag = false; out += ' '; }
            else if (!inTag) out += c;
        }
        return out;
    }

    std::string url_;
};
