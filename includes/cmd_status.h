#pragma once
/**
 * cmd_status.h
 * ------------
 * `mini_vcs status` — show the state of the working directory.
 *
 * Reports four categories:
 *   - Changes to be committed   (staged: in index, differs from last commit)
 *   - Changes not staged        (modified on disk but not yet staged)
 *   - Untracked files           (on disk, not in index or last commit)
 *   - Deleted files             (in index, missing from disk)
 */

#include "repository.h"

/** Print working-tree status to stdout. */
void cmd_status(const Repository& repo);
