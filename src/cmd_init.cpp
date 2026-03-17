/**
 * cmd_init.cpp
 * ------------
 * Implementation of `mini_vcs init` with interactive strategy selection.
 */

#include "cmd_init.h"
#include "repo_config.h"
#include "utils.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// ── Strategy selection prompt ─────────────────────────────────────────────────

static CommitStrategy prompt_strategy() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           Mini-VCS  —  Choose a Commit Strategy             ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  1) FULL_COPY       Every file copied in every commit        ║\n";
    std::cout << "║                    + Simplest   - Most storage              ║\n";
    std::cout << "║                                                              ║\n";
    std::cout << "║  2) DELTA_METADATA Only changed files stored;               ║\n";
    std::cout << "║                    change detected via mtime + file size    ║\n";
    std::cout << "║                    + Very fast  + Low storage               ║\n";
    std::cout << "║                    - May miss silent same-size edits        ║\n";
    std::cout << "║                                                              ║\n";
    std::cout << "║  3) DELTA_HASH     Only changed files stored;               ║\n";
    std::cout << "║                    change detected via SHA-1 content hash   ║\n";
    std::cout << "║                    + Accurate   + Low storage               ║\n";
    std::cout << "║                    - Slightly slower (hashes every file)    ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\nEnter choice [1/2/3] (default: 3): ";
    std::cout.flush();

    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    if (line.empty()) line = "3";

    try {
        return parse_strategy(line);
    } catch (...) {
        std::cout << "Invalid choice, defaulting to DELTA_HASH (3)\n";
        return CommitStrategy::DELTA_HASH;
    }
}

// ── cmd_init ──────────────────────────────────────────────────────────────────

bool cmd_init(Repository& repo, std::optional<CommitStrategy> strategy) {
    if (fs::exists(repo.git_dir)) return false;

    CommitStrategy chosen = strategy.has_value() ? *strategy : prompt_strategy();

    fs::create_directories(repo.objects_dir);
    fs::create_directories(repo.heads_dir);

    write_file(repo.head_file, "ref: refs/heads/master\n");
    save_config(repo.git_dir, chosen);
    repo.save_index({});

    std::cout << "\nInitialized empty Mini-VCS repository in " << repo.git_dir << "\n";
    std::cout << "Commit strategy : " << strategy_name(chosen) << "\n";
    std::cout << "                  " << strategy_description(chosen) << "\n\n";
    return true;
}
