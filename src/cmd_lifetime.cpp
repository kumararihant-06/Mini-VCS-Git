/**
 * cmd_lifetime.cpp
 * ----------------
 * Implementation of `mini_vcs lifetime <filename>`.
 *
 * Algorithm:
 *   1. Walk the entire commit chain from HEAD (oldest → newest after reversal).
 *   2. For each commit, resolve the file's blob via the commit's root tree.
 *      Handles sentinel blobs (Strategies 2 & 3) by following "ref:<hash>" chains.
 *   3. Record size (or -1 if absent) and commit metadata.
 *   4. Compute per-commit deltas and aggregate statistics.
 *   5. Render the table, ASCII bar chart, and summary.
 */

#include "cmd_lifetime.h"
#include "objects.h"
#include "utils.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

/** Format a Unix timestamp as "Mon DD HH:MM" (similar to git log --date=short). */
std::string format_date(long long ts) {
    time_t t = static_cast<time_t>(ts);
    char   buf[32];
    struct tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%b %d %H:%M", tm_info);
    return buf;
}

/** Return the first 7 characters of a 40-char hash. */
std::string short_hash(const std::string& h) {
    return h.size() >= 7 ? h.substr(0, 7) : h;
}

/** Truncate a string to at most `max_len` chars; append "…" if truncated. */
std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len - 1) + "\xe2\x80\xa6"; // UTF-8 ellipsis
}

/** Format a signed byte delta as "+12B", "-3B", or "--" (unchanged). */
std::string delta_str(long long prev, long long cur) {
    if (prev < 0 && cur < 0) return "  --  ";   // still absent
    if (prev < 0)             return "+" + std::to_string(cur) + "B"; // appeared
    if (cur  < 0)             return "gone";     // deleted
    long long d = cur - prev;
    if (d == 0) return "  --  ";
    return (d > 0 ? "+" : "") + std::to_string(d) + "B";
}

/**
 * Given a commit's root tree hash and a relative file path (e.g. "src/foo.cpp"),
 * return the blob hash for that file, or an empty string if the file is absent.
 *
 * Handles nested directories by recursively descending through sub-trees.
 */
std::string find_blob_in_tree(const Repository&  repo,
                               const std::string& tree_hash,
                               const std::string& filepath)
{
    // Split filepath on '/'
    auto parts = split(filepath, '/');
    std::string cur_tree = tree_hash;

    for (size_t i = 0; i < parts.size(); ++i) {
        try {
            auto obj  = repo.load_object(cur_tree);
            if (obj.type != "tree") return "";
            auto tree = Tree::from_content(obj.content);

            bool found = false;
            for (const auto& e : tree.entries) {
                if (e.name == parts[i]) {
                    if (i + 1 == parts.size()) {
                        // Leaf: return the blob hash
                        return e.obj_hash;
                    } else {
                        // Directory: descend
                        cur_tree = e.obj_hash;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) return "";
        } catch (...) {
            return "";
        }
    }
    return "";
}

/**
 * Resolve a blob hash to its actual byte-content size.
 * Handles sentinel blobs ("ref:<commit_hash>") used by commit strategies 2 & 3
 * by following the reference chain (capped at 64 hops).
 */
long long blob_size(const Repository&  repo,
                    const std::string& blob_hash,
                    const std::string& filepath)
{
    std::string cur = blob_hash;
    for (int hop = 0; hop < 64 && !cur.empty(); ++hop) {
        try {
            auto obj = repo.load_object(cur);
            if (obj.content.rfind("ref:", 0) == 0) {
                // Sentinel: follow the reference
                std::string ref_commit = obj.content.substr(4);
                auto cd  = CommitData::from_content(
                               repo.load_object(ref_commit).content);
                auto idx = repo.index_from_tree(cd.tree_hash);
                if (idx.count(filepath)) {
                    cur = idx.at(filepath);
                    continue;
                }
                return -1; // couldn't resolve
            }
            // Real blob
            return static_cast<long long>(obj.content.size());
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

// ── Row data ─────────────────────────────────────────────────────────────────

struct LifetimeRow {
    std::string commit_hash;
    std::string short_date;
    long long   size;       // -1 = file absent in this commit
    std::string delta;
    std::string message;
};

} // anonymous namespace

// ── Public entry point ────────────────────────────────────────────────────────

void cmd_lifetime(const Repository& repo, const std::string& filename) {

    // ── Step 1: collect full commit chain (newest → oldest) ──────────────────
    std::string branch = repo.current_branch();
    std::string hash   = repo.get_branch_commit(branch);

    if (hash.empty()) {
        std::cout << "No commits yet.\n";
        return;
    }

    // Collect commits newest-first, then reverse to oldest-first for display
    struct RawCommit {
        std::string hash;
        CommitData  data;
    };
    std::vector<RawCommit> commits;

    {
        std::string cur = hash;
        while (!cur.empty()) {
            try {
                auto obj = repo.load_object(cur);
                auto cd  = CommitData::from_content(obj.content);
                commits.push_back({cur, cd});
                cur = cd.parent_hashes.empty() ? "" : cd.parent_hashes[0];
            } catch (...) {
                break;
            }
        }
    }

    // Reverse so we go oldest → newest
    std::reverse(commits.begin(), commits.end());

    // ── Step 2-4: build table rows ───────────────────────────────────────────
    std::vector<LifetimeRow> rows;
    long long prev_size = -1;

    for (const auto& rc : commits) {
        long long cur_size = -1;

        // Find file in this commit's tree
        std::string blob = find_blob_in_tree(repo, rc.data.tree_hash, filename);
        if (!blob.empty()) {
            cur_size = blob_size(repo, blob, filename);
        }

        LifetimeRow row;
        row.commit_hash = rc.hash;
        row.short_date  = format_date(rc.data.timestamp);
        row.size        = cur_size;
        row.delta       = delta_str(prev_size, cur_size);
        row.message     = rc.data.message;

        rows.push_back(row);
        prev_size = cur_size;
    }

    // ── Step 5: render ───────────────────────────────────────────────────────

    const std::string line(58, '-');

    std::cout << "\nFile Lifetime Report: " << filename << "\n";
    std::cout << line << "\n";
    std::cout << std::left
              << std::setw(9)  << "Commit"
              << std::setw(16) << "Date"
              << std::setw(9)  << "Size"
              << std::setw(10) << "Change"
              << "Message\n";
    std::cout << line << "\n";

    // Gather sizes for bar chart (only present versions)
    std::vector<long long> present_sizes;
    for (const auto& r : rows)
        if (r.size >= 0) present_sizes.push_back(r.size);

    long long peak_size   = present_sizes.empty() ? 0
                          : *std::max_element(present_sizes.begin(), present_sizes.end());

    for (const auto& r : rows) {
        std::string sz_str = (r.size < 0) ? "(absent)" : (std::to_string(r.size) + "B");
        std::cout << std::left
                  << std::setw(9)  << short_hash(r.commit_hash)
                  << std::setw(16) << r.short_date
                  << std::setw(9)  << sz_str
                  << std::setw(10) << r.delta
                  << truncate(r.message, 24) << "\n";
    }

    std::cout << line << "\n";

    // ── ASCII bar chart ───────────────────────────────────────────────────────
    const int BAR_MAX   = 40;       // max bar width in chars
    const int BYTES_PER = std::max(1LL, (peak_size + BAR_MAX - 1) / BAR_MAX);

    std::cout << "\nSize over time (each # = " << BYTES_PER << " byte"
              << (BYTES_PER == 1 ? "" : "s") << "):\n";

    for (const auto& r : rows) {
        if (r.size < 0) {
            std::cout << "  " << std::setw(8) << "(absent)" << "  |\n";
        } else {
            int bars = static_cast<int>(r.size / BYTES_PER);
            if (bars == 0 && r.size > 0) bars = 1; // show at least one bar
            std::string sz_lbl = std::to_string(r.size) + "B";
            std::cout << "  " << std::setw(8) << sz_lbl << "  |"
                      << std::string(bars, '#') << "\n";
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    int total_commits = static_cast<int>(rows.size());

    // Count commits where size actually changed (churn)
    int changed = 0;
    long long prev = -1;
    for (const auto& r : rows) {
        if (r.size != prev && !(r.size < 0 && prev < 0)) {
            if (prev >= 0 || r.size >= 0) ++changed;
        }
        prev = r.size;
    }

    // Net growth: last_present - first_present
    long long first_present = -1, last_present = -1;
    std::string peak_commit;
    for (const auto& r : rows) {
        if (r.size >= 0) {
            if (first_present < 0) first_present = r.size;
            last_present = r.size;
        }
        if (r.size == peak_size && peak_commit.empty())
            peak_commit = short_hash(r.commit_hash);
    }

    long long net_growth = (first_present >= 0 && last_present >= 0)
                         ? last_present - first_present : 0;

    double churn_rate = total_commits > 0
                      ? (100.0 * changed / total_commits) : 0.0;

    std::cout << "\n" << line << "\n";
    std::cout << "Total lifetime  : " << total_commits << " commit"
              << (total_commits == 1 ? "" : "s") << "\n";
    std::cout << "Times changed   : " << changed << " out of " << total_commits
              << " commits (" << std::fixed << std::setprecision(0)
              << churn_rate << "% churn rate)\n";

    if (net_growth >= 0)
        std::cout << "Net growth      : +" << net_growth << "B from first to last version\n";
    else
        std::cout << "Net growth      : " << net_growth << "B from first to last version\n";

    if (peak_size > 0)
        std::cout << "Peak size       : " << peak_size << "B at commit " << peak_commit << "\n";

    std::cout << line << "\n\n";
}
