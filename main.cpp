/**
 * main.cpp
 * --------
 * Mini-VCS — CLI entry point.
 *
 * This file only handles argument parsing and dispatches each sub-command
 * to its dedicated module.  No business logic lives here.
 *
 * Module layout:
 *
 *   include/
 *     utils.h        ← hex, I/O, zlib, SHA-1, string helpers
 *     objects.h      ← GitObject, Blob, Tree, CommitData
 *     index.h        ← staging-area load / save
 *     repository.h   ← Repository class (paths + shared object store)
 *     cmd_init.h     ← `init`     command
 *     cmd_add.h      ← `add`      command
 *     cmd_commit.h   ← `commit`   command
 *     cmd_checkout.h ← `checkout` command
 *     cmd_branch.h   ← `branch`   command
 *     cmd_log.h      ← `log`      command
 *     cmd_status.h   ← `status`   command
 *
 * Build:
 *   g++ -std=c++17 -O2 -Iinclude \
 *       src/utils.cpp src/objects.cpp src/index.cpp src/repository.cpp \
 *       src/cmd_init.cpp src/cmd_add.cpp src/cmd_commit.cpp \
 *       src/cmd_checkout.cpp src/cmd_branch.cpp \
 *       src/cmd_log.cpp src/cmd_status.cpp \
 *       main.cpp \
 *       -lz -lcrypto -o mini_vcs
 */

#include "cmd_add.h"
#include "cmd_branch.h"
#include "cmd_checkout.h"
#include "cmd_commit.h"
#include "cmd_init.h"
#include "cmd_log.h"
#include "cmd_status.h"
#include "commit_strategy.h"
#include "repository.h"
#include "cmd_lifetime.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// ── Usage ─────────────────────────────────────────────────────────────────────

static void print_usage() {
    std::cout << R"(
Mini-VCS — a Git-like version control system (C++)

Usage:  mini_vcs <command> [options]

Commands:
  init                     Initialize a new repository (.git/)
  add <path> [paths...]    Stage file(s) or directory for the next commit
  commit -m "message"      Create a commit from staged files
         [--author "Name <email>"]
  checkout <branch>        Switch to an existing branch
  checkout -b <branch>     Create and switch to a new branch
  branch                   List all branches
  branch <name>            Create a new branch
  branch <name> -d         Delete a branch
  log                      Show commit history (last 10)
  log -n <N>               Show last N commits
  status                   Show working-tree and staging-area status
  lifetime <file>          Show full size history of a file across all commits
)";
}

// ── Guard: ensure we're inside a repository ───────────────────────────────────

static bool require_repo(const Repository& repo) {
    if (!fs::exists(repo.git_dir)) {
        std::cerr << "Error: not a Mini-VCS repository "
                     "(run 'mini_vcs init' first)\n";
        return false;
    }
    return true;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 0; }

    std::string cmd = argv[1];
    Repository  repo(".");

    try {
        // ── init ──────────────────────────────────────────────────────────────
        if (cmd == "init") {
            // Optional: mini_vcs init --strategy <1|2|3>
            std::optional<CommitStrategy> preset;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if ((a == "--strategy" || a == "-s") && i + 1 < argc)
                    preset = parse_strategy(argv[++i]);
            }
            if (!cmd_init(repo, preset))
                std::cout << "Repository already exists at " << repo.git_dir << "\n";

        // ── add ───────────────────────────────────────────────────────────────
        } else if (cmd == "add") {
            if (!require_repo(repo)) return 1;
            if (argc < 3) { std::cerr << "Usage: mini_vcs add <path> [paths...]\n"; return 1; }
            for (int i = 2; i < argc; ++i)
                cmd_add(repo, argv[i]);

        // ── commit ────────────────────────────────────────────────────────────
        } else if (cmd == "commit") {
            if (!require_repo(repo)) return 1;
            std::string message, author;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if ((a == "-m" || a == "--message") && i + 1 < argc) message = argv[++i];
                else if (a == "--author"             && i + 1 < argc) author  = argv[++i];
            }
            if (message.empty()) {
                std::cerr << "Error: commit requires -m \"message\"\n";
                return 1;
            }
            cmd_commit(repo, message, author);

        // ── checkout ──────────────────────────────────────────────────────────
        } else if (cmd == "checkout") {
            if (!require_repo(repo)) return 1;
            bool        create_new = false;
            std::string branch;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "-b" || a == "--create-branch") create_new = true;
                else branch = a;
            }
            if (branch.empty()) {
                std::cerr << "Error: checkout requires a branch name\n";
                return 1;
            }
            cmd_checkout(repo, branch, create_new);

        // ── branch ────────────────────────────────────────────────────────────
        } else if (cmd == "branch") {
            if (!require_repo(repo)) return 1;
            bool        del = false;
            std::string name;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "-d" || a == "--delete") del = true;
                else name = a;
            }
            cmd_branch(repo, name, del);

        // ── log ───────────────────────────────────────────────────────────────
        } else if (cmd == "log") {
            if (!require_repo(repo)) return 1;
            int n = 10;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if ((a == "-n" || a == "--max-count") && i + 1 < argc)
                    n = std::stoi(argv[++i]);
            }
            cmd_log(repo, n);

        // ── status ────────────────────────────────────────────────────────────
        } else if (cmd == "status") {
            if (!require_repo(repo)) return 1;
            cmd_status(repo);

        // ── unknown ───────────────────────────────────────────────────────────
        } else if (cmd == "lifetime") {
            if (!require_repo(repo)) return 1;
            if (argc < 3) {
                std::cerr << "Usage: mini_vcs lifetime <filename>\n";
                return 1;
            }
            cmd_lifetime(repo, argv[2]);

        // ── unknown ───────────────────────────────────────────────────────────
        }else {
            std::cerr << "Unknown command: " << cmd << "\n";
            print_usage();
            return 1;
        }

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
