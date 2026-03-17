#pragma once
/**
 * index.h
 * -------
 * The Index (staging area) module.
 *
 * Mirrors Git's .git/index behaviour:
 *   - A flat map of { relative-path → blob-SHA1-hex }
 *   - Persisted as a JSON file at .git/index
 *   - Updated by `add` and cleared after `commit`
 */

#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

/** Load the staging-area map from disk.  Returns empty map if missing. */
std::map<std::string, std::string> load_index(const fs::path& index_file);

/** Persist the staging-area map to disk. */
void save_index(const fs::path& index_file,
                const std::map<std::string, std::string>& idx);
