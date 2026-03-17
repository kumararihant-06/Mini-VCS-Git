#pragma once
/**
 * cmd_commit.h
 * ------------
 * `mini_vcs commit -m "message"` — snapshot the staging area.
 *
 * The actual storage behaviour depends on the CommitStrategy that was
 * chosen at `init` time and is read from .git/mini_vcs_config.
 *
 * Strategy 1 – FULL_COPY
 *   Every staged file is written as a new Blob unconditionally.
 *
 * Strategy 2 – DELTA_METADATA
 *   Compares each file's mtime + size against the cached values from
 *   the previous commit.  Only files that differ get a new Blob;
 *   unchanged files get a lightweight "ref:<prev_commit>" Blob that
 *   checkout resolves transparently.
 *
 * Strategy 3 – DELTA_HASH
 *   Computes the SHA-1 of each file and compares it to the blob hash
 *   stored in the previous commit's tree.  Only truly modified files
 *   get a new Blob; the rest get the same "ref:<prev_commit>" pointer.
 *
 * All three strategies produce the same commit graph and checkout
 * behaviour; they differ only in how much data they copy each commit,
 * how long the commit takes, and how much disk space they consume.
 *
 * After the commit, timing and storage stats are printed.
 */

#include "repository.h"
#include "commit_strategy.h"
#include <string>

/**
 * Run the commit command using the strategy stored in .git/mini_vcs_config.
 *
 * @param repo    open Repository
 * @param message commit message (non-empty)
 * @param author  "Name <email>" (empty = default)
 * @return        CommitResult (commit_hash empty if nothing to commit)
 */
CommitResult cmd_commit(Repository&        repo,
                        const std::string& message,
                        const std::string& author = "");
