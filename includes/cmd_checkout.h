#pragma once
/**
 * cmd_checkout.h
 * --------------
 * `mini_vcs checkout [-b] <branch>` — switch working directory to a branch.
 *
 * Steps:
 *   1. Collect all files tracked by the current branch (to remove them).
 *   2. If -b: create the new branch pointing at the current commit.
 *   3. Update HEAD to reference the target branch.
 *   4. Delete files that belonged to the old branch.
 *   5. Restore all files from the target branch's commit tree.
 *   6. Clear the staging index.
 */

#include "repository.h"
#include <string>

/**
 * Switch to (or create) `branch`.
 *
 * @param repo        open Repository
 * @param branch      target branch name
 * @param create_new  true when -b flag was passed
 */
void cmd_checkout(Repository& repo,
                  const std::string& branch,
                  bool create_new);
