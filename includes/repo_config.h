#pragma once
/**
 * repo_config.h
 * -------------
 * Persists repository-level configuration inside .git/mini_vcs_config:
 *
 *   strategy=<1|2|3>
 *
 * Also manages the metadata cache used by Strategy 2 (DELTA_METADATA).
 * The cache lives at .git/meta_cache and stores:
 *
 *   <relative-path> <mtime_unix_sec> <file_size_bytes> <last_commit_hash>
 *
 * one entry per line.  Strategy 3 (DELTA_HASH) reuses the existing
 * blob-hash from the index as its "last known hash", so it does not
 * need a separate cache.
 */

#include "commit_strategy.h"

#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

// ── Config (strategy choice) ──────────────────────────────────────────────────

/** Write strategy to .git/mini_vcs_config. */
void save_config(const fs::path& git_dir, CommitStrategy s);

/** Read strategy from .git/mini_vcs_config.  Throws if missing. */
CommitStrategy load_config(const fs::path& git_dir);

// ── Metadata cache (Strategy 2) ───────────────────────────────────────────────

struct FileMeta {
    long long   mtime_sec  = 0;
    uintmax_t   size_bytes = 0;
    std::string last_commit_hash; ///< commit that stores this file's content
};

using MetaCache = std::map<std::string /*rel_path*/, FileMeta>;

/** Load metadata cache from .git/meta_cache. */
MetaCache load_meta_cache(const fs::path& git_dir);

/** Save metadata cache to .git/meta_cache. */
void save_meta_cache(const fs::path& git_dir, const MetaCache& cache);

/** Read live filesystem metadata for a file. */
FileMeta read_file_meta(const fs::path& full_path, const std::string& last_commit);
