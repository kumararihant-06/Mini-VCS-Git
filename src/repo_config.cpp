/**
 * repo_config.cpp
 * ---------------
 * Read/write repository config and the metadata cache used by Strategy 2.
 */

#include "repo_config.h"
#include "utils.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

// ── Config ────────────────────────────────────────────────────────────────────

void save_config(const fs::path& git_dir, CommitStrategy s) {
    fs::path cfg = git_dir / "mini_vcs_config";
    write_file(cfg, "strategy=" + std::to_string((int)s) + "\n");
}

CommitStrategy load_config(const fs::path& git_dir) {
    fs::path cfg = git_dir / "mini_vcs_config";
    if (!fs::exists(cfg))
        throw std::runtime_error(
            "Repository config not found. Was this repo initialised with mini_vcs init?");
    std::string raw = trim(read_file(cfg));
    // raw looks like "strategy=2"
    auto eq = raw.find('=');
    if (eq == std::string::npos)
        throw std::runtime_error("Malformed mini_vcs_config");
    std::string val = raw.substr(eq + 1);
    return parse_strategy(val);
}

// ── Metadata cache ────────────────────────────────────────────────────────────

MetaCache load_meta_cache(const fs::path& git_dir) {
    MetaCache cache;
    fs::path  path = git_dir / "meta_cache";
    if (!fs::exists(path)) return cache;

    std::ifstream f(path);
    std::string   line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // format: "<rel_path> <mtime> <size> <commit_hash>"
        std::istringstream ss(line);
        std::string rel, commit;
        long long   mtime;
        uintmax_t   sz;
        if (ss >> rel >> mtime >> sz >> commit)
            cache[rel] = {mtime, sz, commit};
    }
    return cache;
}

void save_meta_cache(const fs::path& git_dir, const MetaCache& cache) {
    fs::path    path = git_dir / "meta_cache";
    std::string out;
    for (const auto& [rel, m] : cache)
        out += rel + " " + std::to_string(m.mtime_sec) + " "
             + std::to_string(m.size_bytes) + " " + m.last_commit_hash + "\n";
    write_file(path, out);
}

FileMeta read_file_meta(const fs::path& full_path, const std::string& last_commit) {
    FileMeta m;
    m.last_commit_hash = last_commit;
    m.size_bytes       = fs::file_size(full_path);

    // Portable mtime: cast file_time_type duration to seconds since epoch
    auto ftime = fs::last_write_time(full_path);
    // file_time_type epoch may differ from unix epoch; use duration cast
    auto dur = ftime.time_since_epoch();
    m.mtime_sec = std::chrono::duration_cast<std::chrono::seconds>(dur).count();
    return m;
}
