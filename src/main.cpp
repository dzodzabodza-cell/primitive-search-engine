#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "engine.hpp"

namespace {

// Splits a command line into words; "double quoted text" stays together.
std::vector<std::string> splitArgs(const std::string& line) {
    std::vector<std::string> args;
    std::string cur;
    bool quoted = false, have = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            have = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
            if (have) args.push_back(cur);
            cur.clear();
            have = false;
        } else {
            cur += c;
            have = true;
        }
    }
    if (have) args.push_back(cur);
    return args;
}

void printHelp() {
    std::puts(
        "Commands:\n"
        "  search <keywords> [options]   ranked keyword search\n"
        "      -c, --category <name>     only this category (quote multi-word names)\n"
        "      -f, --from <YYYY-MM-DD>   only documents on/after this date\n"
        "      -t, --to <YYYY-MM-DD>     only documents on/before this date\n"
        "      -n, --limit <N>           number of results (default 10)\n"
        "      --any                     match ANY keyword (default: ALL keywords)\n"
        "  browse [category] [page]      list documents, newest first, 10 per page\n"
        "  categories                    list categories with document counts\n"
        "  show <id>                     print a whole document\n"
        "  load csv <path>               add a local CSV file (title,category,date,body)\n"
        "  load dir <path>               add a folder of .txt/.md files\n"
        "  load url <http(s)://...>      add a remote page or CSV\n"
        "  reindex                       reload all sources from scratch\n"
        "  stats                         index statistics\n"
        "  help, quit");
}

void printResults(const std::vector<Result>& results, size_t total) {
    if (results.empty()) {
        std::puts("No results.");
        return;
    }
    for (size_t i = 0; i < results.size(); ++i) {
        const Result& r = results[i];
        std::printf("%2zu. [#%d] %s\n    %s | %s", i + 1, r.doc->id, r.doc->title.c_str(),
                    r.doc->category.c_str(), formatDate(r.doc->date).c_str());
        if (r.score > 0) std::printf(" | score %.2f", r.score);
        std::printf("\n    %s\n", r.snippet.c_str());
    }
    std::printf("Showing %zu of %zu result(s).\n", results.size(), total);
}

bool parseInt(const std::string& s, long& out) {
    char* end = nullptr;
    out = std::strtol(s.c_str(), &end, 10);
    return !s.empty() && *end == '\0';
}

void cmdSearch(const SearchEngine& engine, const std::vector<std::string>& args) {
    Query q;
    std::string text;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= args.size()) {
                std::printf("Missing value after %s\n", a.c_str());
                return false;
            }
            dst = args[++i];
            return true;
        };
        std::string v;
        if (a == "-c" || a == "--category") {
            if (!next(q.category)) return;
        } else if (a == "-f" || a == "--from") {
            if (!next(v)) return;
            if (!(q.from = parseDate(v))) { std::puts("Invalid --from date (use YYYY-MM-DD)."); return; }
        } else if (a == "-t" || a == "--to") {
            if (!next(v)) return;
            if (!(q.to = parseDate(v))) { std::puts("Invalid --to date (use YYYY-MM-DD)."); return; }
        } else if (a == "-n" || a == "--limit") {
            long n;
            if (!next(v)) return;
            if (!parseInt(v, n) || n < 1) { std::puts("Invalid --limit."); return; }
            q.limit = static_cast<size_t>(n);
        } else if (a == "--any") {
            q.matchAll = false;
        } else {
            text += a + " ";
        }
    }
    q.text = text;
    if (q.text.empty() && q.category.empty() && !q.from && !q.to) {
        std::puts("Usage: search <keywords> [-c category] [-f date] [-t date] [-n N] [--any]");
        return;
    }
    if (!q.text.empty() && Tokenizer::tokenize(q.text).empty()) {
        std::puts("Your keywords are too common or too short to search for.");
        return;
    }
    printResults(engine.search(q), engine.countMatches(q));
}

void cmdBrowse(const SearchEngine& engine, const std::vector<std::string>& args) {
    Query q;
    long page = 1;
    for (size_t i = 1; i < args.size(); ++i) {
        long n;
        if (parseInt(args[i], n) && n >= 1) page = n;
        else q.category = args[i];
    }
    constexpr size_t kPage = 10;
    q.limit = static_cast<size_t>(page) * kPage;
    std::vector<Result> all = engine.search(q);
    const size_t total = engine.countMatches(q);
    const size_t begin = (static_cast<size_t>(page) - 1) * kPage;
    if (begin >= all.size()) { std::puts("No more documents."); return; }
    std::vector<Result> slice(all.begin() + static_cast<long>(begin), all.end());
    for (size_t i = 0; i < slice.size(); ++i)
        std::printf("%2zu. [#%d] %s  (%s, %s)\n", begin + i + 1, slice[i].doc->id,
                    slice[i].doc->title.c_str(), slice[i].doc->category.c_str(),
                    formatDate(slice[i].doc->date).c_str());
    std::printf("Page %ld of %zu.\n", page, (total + kPage - 1) / kPage);
}

void cmdLoad(SearchEngine& engine, const std::vector<std::string>& args) {
    if (args.size() < 3) { std::puts("Usage: load csv|dir|url <location>"); return; }
    std::unique_ptr<DataSource> src;
    if (args[1] == "csv") src = std::make_unique<CsvSource>(args[2]);
    else if (args[1] == "dir") src = std::make_unique<DirectorySource>(args[2]);
    else if (args[1] == "url") src = std::make_unique<RemoteSource>(args[2]);
    else { std::puts("Unknown source type. Use csv, dir or url."); return; }
    try {
        std::printf("Loaded %zu document(s).\n", engine.addSource(std::move(src)));
    } catch (const std::exception& e) {
        std::printf("Error: %s\n", e.what());
    }
}

void cmdShow(const SearchEngine& engine, const std::vector<std::string>& args) {
    long id;
    if (args.size() < 2 || !parseInt(args[1], id)) { std::puts("Usage: show <id>"); return; }
    const Document* d = engine.get(static_cast<int>(id));
    if (!d) { std::puts("No such document."); return; }
    std::printf("#%d  %s\nCategory: %s\nDate: %s\nSource: %s\n\n%s\n", d->id, d->title.c_str(),
                d->category.c_str(), formatDate(d->date).c_str(), d->source.c_str(), d->body.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    SearchEngine engine;

    // Optional start-up sources: --csv <file> | --dir <folder> | --url <url>
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "-h" || flag == "--help") {
            std::puts("Usage: search_engine [--csv file] [--dir folder] [--url address]...");
            return 0;
        }
        if (i + 1 >= argc) { std::fprintf(stderr, "Missing value for %s\n", flag.c_str()); return 1; }
        std::unique_ptr<DataSource> src;
        const std::string value = argv[++i];
        if (flag == "--csv") src = std::make_unique<CsvSource>(value);
        else if (flag == "--dir") src = std::make_unique<DirectorySource>(value);
        else if (flag == "--url") src = std::make_unique<RemoteSource>(value);
        else { std::fprintf(stderr, "Unknown option %s\n", flag.c_str()); return 1; }
        try {
            const size_t n = engine.addSource(std::move(src));
            std::printf("Loaded %zu document(s) from %s\n", n, value.c_str());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Error loading %s: %s\n", value.c_str(), e.what());
        }
    }

    std::printf("Search Engine ready: %zu documents, %zu terms. Type 'help' for commands.\n",
                engine.docCount(), engine.termCount());

    std::string line;
    while (std::printf("> "), std::fflush(stdout), std::getline(std::cin, line)) {
        const std::vector<std::string> args = splitArgs(line);
        if (args.empty()) continue;
        const std::string& cmd = args[0];
        if (cmd == "quit" || cmd == "exit") break;
        else if (cmd == "help") printHelp();
        else if (cmd == "search" || cmd == "s") cmdSearch(engine, args);
        else if (cmd == "browse") cmdBrowse(engine, args);
        else if (cmd == "show") cmdShow(engine, args);
        else if (cmd == "load") cmdLoad(engine, args);
        else if (cmd == "reindex") std::printf("Reindexed %zu document(s).\n", engine.reindex());
        else if (cmd == "categories") {
            for (const auto& [name, n] : engine.categories()) std::printf("  %-20s %zu\n", name.c_str(), n);
        } else if (cmd == "stats") {
            std::printf("Sources: %zu\nDocuments: %zu\nUnique terms: %zu\n", engine.sourceCount(),
                        engine.docCount(), engine.termCount());
        } else std::puts("Unknown command. Type 'help'.");
    }
    std::puts("");
    return 0;
}
