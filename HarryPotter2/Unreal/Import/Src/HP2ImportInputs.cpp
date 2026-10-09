#include "HP2ImportInternal.h"
namespace HP2Import::Detail {
std::vector<fs::path> DeterministicFiles(const fs::path& root) {
    std::vector<fs::path> files;
    std::vector<fs::path> pending{root};
    while (!pending.empty()) {
        fs::path current = std::move(pending.back());
        pending.pop_back();
        std::vector<fs::path> entries;
        std::error_code ec;
        for (fs::directory_iterator it(current, ec), end; it != end && !ec; it.increment(ec))
            entries.push_back(it->path());
        if (ec) Fail("cannot inspect " + Utf8(current) + ": " + ec.message());
        std::sort(entries.begin(), entries.end(), [](const fs::path& a, const fs::path& b) {
            const std::string x = Utf8(a.filename()), y = Utf8(b.filename());
            const std::string lx = Lower(x), ly = Lower(y);
            return lx == ly ? x < y : lx < ly;
        });
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            auto st = fs::symlink_status(*it, ec);
            if (ec) Fail("cannot inspect " + Utf8(*it) + ": " + ec.message());
            if (fs::is_symlink(st)) Fail("symbolic link in input tree: " + Utf8(*it));
            if (fs::is_directory(st)) pending.push_back(*it);
            else if (fs::is_regular_file(st)) files.push_back(*it);
            else Fail("unsupported input entry: " + Utf8(*it));
        }
    }
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
        const std::string x = Utf8(a.lexically_relative(root)), y = Utf8(b.lexically_relative(root));
        const std::string lx = Lower(x), ly = Lower(y);
        return lx == ly ? x < y : lx < ly;
    });
    return files;
}
}
