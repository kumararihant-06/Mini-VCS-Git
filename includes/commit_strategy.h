#pragma once
/**
 * commit_strategy.h
 * -----------------
 * Defines the three commit storage strategies and the shared
 * CommitResult struct that carries timing + storage stats.
 *
 * Strategy 1 – FULL_COPY
 *   Every tracked file is stored fresh in every commit.
 *   Simple, self-contained, but most expensive in disk space.
 *
 * Strategy 2 – DELTA_METADATA
 *   Only files whose filesystem metadata (mtime + size) has changed
 *   since the last commit are stored. Unchanged files carry a
 *   "ref:<commit_hash>" blob that points back to the commit where
 *   their content was last stored.  Fast (no file reads), but can
 *   miss silent in-place edits that keep the same mtime/size.
 *
 * Strategy 3 – DELTA_HASH
 *   Only files whose SHA-1 content hash has changed since the last
 *   commit are stored. Unchanged files carry the same "ref:<hash>"
 *   pointer. More reliable than metadata alone (catches every real
 *   change), slightly slower (must hash every file).
 *
 * For strategies 2 and 3, checkout resolves ref-blobs transparently
 * so the user always gets the correct file content.
 */

#include <chrono>
#include <string>

// ── Strategy enum ─────────────────────────────────────────────────────────────

enum class CommitStrategy {
    FULL_COPY      = 1,   ///< copy every file on every commit
    DELTA_METADATA = 2,   ///< deduplicate via mtime+size metadata
    DELTA_HASH     = 3,   ///< deduplicate via SHA-1 content hash
};

/** Parse "1"/"2"/"3" (or the full name) into a CommitStrategy. */
CommitStrategy parse_strategy(const std::string& s);

/** Human-readable name for display. */
std::string strategy_name(CommitStrategy s);

/** One-line description for display. */
std::string strategy_description(CommitStrategy s);

// ── Result struct ─────────────────────────────────────────────────────────────

struct CommitResult {
    std::string commit_hash;          ///< SHA-1 of the new commit (empty = nothing to commit)
    long long   elapsed_ms  = 0;      ///< wall-clock time in milliseconds
    size_t      bytes_stored = 0;     ///< new bytes written to .git/objects this commit
    int         files_stored = 0;     ///< number of blobs physically written
    int         files_referenced = 0; ///< number of blobs reused via reference (strategies 2/3)
};

/** Pretty-print the CommitResult summary to stdout. */
void print_commit_result(const CommitResult& r, CommitStrategy s);
