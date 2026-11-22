// Difficulty table parsing, URL resolution, level order, matching and cache (no network).
#include "DifficultyTable.h"
#include "DifficultyTableFetch.h"
#include <iostream>
#include <stdexcept>

using namespace bms::table;

static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

int main(int argc, char** argv) {
    // Manual check, not run by ctest: BmsDifficultyTableTests --fetch <table URL> downloads and summarizes it.
    if (argc > 2 && std::string(argv[1]) == "--fetch") {
        const auto result = fetchTable(decodeText(argv[2]));
        if (!result.ok) { std::cerr << "fetch failed: " << utf8(result.error) << std::endl; return 1; }
        std::cout << "header: " << utf8(result.table.headerUrl) << std::endl << "name: " << utf8(result.table.name)
                  << "  symbol: " << utf8(result.table.symbol) << std::endl << "entries: " << result.table.entries.size()
                  << "  levels: " << levels(result.table).size() << std::endl;
        return 0;
    }
    try {
        // The bmstable meta tag, in either attribute order and quote style, among other tags.
        require(findBmstableMeta(L"<html><head><meta charset=\"utf-8\"><meta name=\"bmstable\" content=\"header.json\"></head>") == L"header.json",
            "meta name then content");
        require(findBmstableMeta(L"<META CONTENT='./data/head.json' NAME='BMSTable' />") == L"./data/head.json", "meta content first, upper case");
        require(!findBmstableMeta(L"<meta name=\"description\" content=\"x\">") && !findBmstableMeta(L"plain text"), "no bmstable meta");

        // References resolve against the page or header they came from.
        const std::wstring page = L"https://example.com/tables/insane/index.html";
        require(resolveUrl(page, L"header.json") == L"https://example.com/tables/insane/header.json", "relative file");
        require(resolveUrl(page, L"./data/score.json") == L"https://example.com/tables/insane/data/score.json", "dot relative");
        require(resolveUrl(page, L"../common/head.json") == L"https://example.com/tables/common/head.json", "parent relative");
        require(resolveUrl(page, L"/root.json") == L"https://example.com/root.json", "host relative");
        require(resolveUrl(page, L"//cdn.example.net/x.json") == L"https://cdn.example.net/x.json", "scheme relative");
        require(resolveUrl(page, L"http://other.example.org/d.json") == L"http://other.example.org/d.json", "absolute");
        require(resolveUrl(L"https://example.com", L"h.json") == L"https://example.com/h.json", "base without a path");
        require(resolveUrl(L"https://example.com/a/b.html?v=1", L"c.json") == L"https://example.com/a/c.json", "base with a query");

        // Header and data, with numeric and text levels and an entry that cannot be matched.
        Table table; table.url = page;
        require(parseHeader(LR"({"name":"Test Table","symbol":"★","data_url":"data.json","level_order":[1,2,"3","X"]})", table) &&
            table.name == L"Test Table" && table.symbol == L"★" && table.dataUrl == L"data.json" &&
            table.levelOrder == std::vector<std::wstring>{L"1", L"2", L"3", L"X"}, "header fields");
        Table broken;
        require(!parseHeader(LR"({"name":"no data"})", broken) && !parseHeader(L"<html>", broken), "a header needs data_url");
        const std::wstring md5a(32, L'a'), md5b(32, L'b'), md5c(32, L'c'), sha(64, L'd');
        require(parseData(L"[{\"md5\":\"" + std::wstring(32, L'A') + L"\",\"level\":1,\"title\":\"Song A\"},"
            L"{\"md5\":\"" + md5b + L"\",\"level\":\"2\",\"title\":\"Song B\",\"artist\":\"Someone\"},"
            L"{\"sha256\":\"" + sha + L"\",\"level\":\"2\"},"
            L"{\"md5\":\"" + md5c + L"\",\"level\":12.5},"
            L"{\"md5\":\"short\",\"level\":1},"
            L"{\"md5\":\"" + md5c + L"\",\"level\":\"?!\"},"
            L"{\"md5\":\"" + md5c + L"\"}]", table), "data parses");
        require(table.entries.size() == 6 && table.entries[0].md5 == md5a && table.entries[0].level == L"1" &&
            table.entries[1].artist == L"Someone" && table.entries[3].level == L"12.5" && table.entries[5].level == L"?",
            "entries: MD5 lower-cased, levels as text, an unmatched entry dropped, a missing level is ?");
        require(!parseData(L"{\"not\":\"an array\"}", table) && !parseData(L"[broken", table), "bad data is refused");
        require(parseData(L"[{\"md5\":\"" + md5a + L"\",\"level\":1},{\"md5\":\"" + md5b + L"\",\"level\":\"2\"},{\"sha256\":\"" + sha +
            L"\",\"level\":\"2\"},{\"md5\":\"" + md5c + L"\",\"level\":12.5},{\"md5\":\"" + md5c + L"\",\"level\":\"10\"},{\"md5\":\"" + md5c + L"\",\"level\":\"EX\"}]", table), "data again");

        // Levels: the header's order first, then the rest numerically, then text.
        require(levels(table) == std::vector<std::wstring>{L"1", L"2", L"3", L"X", L"10", L"12.5", L"EX"}, "level order");

        // Owned counts follow the library's digests; SHA-256 entries match too.
        const auto views = levelViews(table, {md5a, md5c}, {sha});
        require(views.size() == 7 && views[0].total == 1 && views[0].owned == 1 && views[1].total == 2 && views[1].owned == 1 &&
            views[2].total == 0 && views[4].owned == 1, "owned and total per level");
        require(levelHas(views[1], L"", sha) && levelHas(views[1], md5b, L"") && !levelHas(views[1], md5a, L""), "level membership");

        // Text decoding: UTF-8 with a BOM, and Shift_JIS where UTF-8 is invalid.
        require(decodeText("\xef\xbb\xbf{\"a\":1}") == L"{\"a\":1}", "UTF-8 BOM");
        require(decodeText("\x93\xef\x88\xd5\x93\x78") == L"難易度", "Shift_JIS fallback");

        // Cache round trip, keyed by the entered URL; a cache of another URL is not taken.
        const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) :
            std::filesystem::temp_directory_path() / L"SevenManiaDifficultyTableTests";
        std::filesystem::remove_all(root);
        table.headerUrl = L"https://example.com/tables/insane/header.json";
        const std::string header = R"({"name":"Test Table","symbol":"★","data_url":"data.json"})";
        const std::string data = "[{\"md5\":\"" + std::string(32, 'a') + "\",\"level\":\"1\"}]";
        require(saveCache(root, table, header, data), "cache saves");
        const auto cached = loadCache(root, page);
        require(cached && cached->name == L"Test Table" && cached->headerUrl == table.headerUrl && cached->entries.size() == 1, "cache loads");
        require(!loadCache(root, L"https://example.com/other.html"), "another URL has no cache");
        removeCache(root, page);
        require(!loadCache(root, page), "removed cache is gone");
        std::filesystem::remove_all(root);
        std::cout << "DifficultyTableTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
