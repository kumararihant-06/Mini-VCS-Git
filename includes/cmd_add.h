#pragma once
/**
 * cmd_add.h
 * ---------
 * `mini_vcs add <path> [paths...]` — stage files for the next commit.
 *
 * Behaviour mirrors `git add`:
 *   - A file path hashes the file and records it in the index.
 *   - A directory path recursively stages all files inside it.
 *   - The .git/ directory is always excluded.
 */

#include "repository.h"
#include <string>

/**
 * Stage a single file at `rel` (relative to repo root).
 * Creates a Blob object and updates the index.
 */
void cmd_add_file(Repository& repo, const std::string& rel);

/**
 * Recursively stage all files inside directory `rel`.
 */
void cmd_add_directory(Repository& repo, const std::string& rel);

/**
 * Dispatch: calls cmd_add_file or cmd_add_directory based on path type.
 */
void cmd_add(Repository& repo, const std::string& rel);
