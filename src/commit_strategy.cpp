/**
 * commit_strategy.cpp
 * -------------------
 * Helpers for CommitStrategy enum: parsing, naming, printing results.
 */

#include "commit_strategy.h"

#include <iostream>
#include <stdexcept>

CommitStrategy parse_strategy(const std::string& s) {
    if (s == "1" || s == "FULL_COPY"      || s == "full")     return CommitStrategy::FULL_COPY;
    if (s == "2" || s == "DELTA_METADATA" || s == "metadata") return CommitStrategy::DELTA_METADATA;
    if (s == "3" || s == "DELTA_HASH"     || s == "hash")     return CommitStrategy::DELTA_HASH;
    throw std::runtime_error("Unknown strategy '" + s + "'. Choose 1, 2, or 3.");
}

std::string strategy_name(CommitStrategy s) {
    switch (s) {
        case CommitStrategy::FULL_COPY:      return "FULL_COPY";
        case CommitStrategy::DELTA_METADATA: return "DELTA_METADATA";
        case CommitStrategy::DELTA_HASH:     return "DELTA_HASH";
    }
    return "UNKNOWN";
}

std::string strategy_description(CommitStrategy s) {
    switch (s) {
        case CommitStrategy::FULL_COPY:
            return "Every file is copied into every commit (safest, most storage)";
        case CommitStrategy::DELTA_METADATA:
            return "Only changed files stored; change detected via mtime+size (fast, low storage)";
        case CommitStrategy::DELTA_HASH:
            return "Only changed files stored; change detected via SHA-1 hash (accurate, low storage)";
    }
    return "";
}

void print_commit_result(const CommitResult& r, CommitStrategy s) {
    if (r.commit_hash.empty()) return; // nothing to commit was already printed

    std::cout << "\n── Commit Stats (" << strategy_name(s) << ") ──────────────────\n";
    std::cout << "  Commit hash  : " << r.commit_hash << "\n";
    std::cout << "  Time taken   : " << r.elapsed_ms  << " ms\n";

    // Format bytes nicely
    auto fmt_bytes = [](size_t b) -> std::string {
        if (b < 1024)       return std::to_string(b) + " B";
        if (b < 1024*1024)  return std::to_string(b/1024) + " KB";
        return std::to_string(b/(1024*1024)) + " MB";
    };

    std::cout << "  Storage used : " << fmt_bytes(r.bytes_stored)
              << "  (" << r.files_stored << " file(s) stored";
    if (r.files_referenced > 0)
        std::cout << ", " << r.files_referenced << " file(s) referenced";
    std::cout << ")\n";
    std::cout << "────────────────────────────────────────────────\n";
}
