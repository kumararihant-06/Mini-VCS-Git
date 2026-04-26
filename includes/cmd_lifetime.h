#pragma once
/**
 * cmd_lifetime.h
 * --------------
 * `mini_vcs lifetime <filename>` — File Lifetime Tracker.
 *
 * Shows the complete life history of a single file across all commits:
 *   - Every commit that exists in history (not just those that touched the file)
 *   - File size at each commit (or "(absent)" if not yet introduced / deleted)
 *   - Byte-level delta from the previous version
 *   - Commit hash (short), date, and message
 *   - ASCII bar chart of size over time
 *   - Summary statistics: total commits, churn rate, net growth, peak size
 *
 * Research connection: Lehman's Laws of Software Evolution (1974).
 * Applying those laws at the individual-file level and making them
 * visible through the VCS itself.
 *
 * Usage:
 *   mini_vcs lifetime hello.txt
 */

#include "repository.h"
#include <string>

/**
 * Print the full lifetime report for `filename` (relative to repo root).
 * Walks the entire commit history from HEAD on the current branch.
 */
void cmd_lifetime(const Repository& repo, const std::string& filename);
