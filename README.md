# Search Engine (C++17)

A command-line search engine built on an inverted index with TF-IDF ranking.
No external libraries (remote loading uses the system `curl`).

## Build and run

    make
    ./search_engine --csv data/articles.csv --dir data/articles
    # or: make run

Start-up options (repeatable): `--csv <file>`, `--dir <folder>`, `--url <http(s) address>`.

## Commands

    search <keywords> [-c category] [-f YYYY-MM-DD] [-t YYYY-MM-DD] [-n N] [--any]
    browse [category] [page]
    categories | show <id> | stats | reindex
    load csv <path> | load dir <path> | load url <address>

Examples:

    search c++ memory
    search sorting algorithms --any -f 2024-01-01
    search "black hole" -c science
    browse history

## Design

| File | Role |
|------|------|
| `document.hpp` | `Document` struct, date parsing/formatting |
| `tokenizer.hpp` | lowercase word splitting, stop words |
| `index.hpp` | `InvertedIndex`: term -> postings, TF-IDF scoring, AND/OR |
| `sources.hpp` | `DataSource` interface; `CsvSource` (structured), `DirectorySource` (.txt/.md, unstructured), `RemoteSource` (URL) |
| `engine.hpp` | `SearchEngine`: owns documents, index and sources; category/date filters; snippets |
| `main.cpp` | interactive command-line interface |

Text files may start with `Title:`, `Category:` and `Date:` header lines; otherwise the
first line is the title, the sub-folder is the category and the file date is used.
CSV files need a header row with `title`, `body`, `category`, `date`.

Matching is exact (no stemming), so `algorithm` will not match `algorithms`.
