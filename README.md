# Mini-VCS

A Git-like version control system built from scratch in C++17. It replicates Git's core object model — content-addressed blob/tree/commit storage, a staging area, branches, and checkout — and adds a research-oriented feature that Git itself doesn't have: **three swappable commit storage strategies** that let you observe the real tradeoffs between simplicity, speed, and storage efficiency at the commit level. It also includes a **File Lifetime Tracker** (`lifetime` command) that visualises a single file's full size history across all commits, connecting directly to Lehman's Laws of Software Evolution.

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Commit Strategies](#commit-strategies)
- [File Lifetime Tracker](#file-lifetime-tracker)
- [Project Structure](#project-structure)
- [Dependencies](#dependencies)
- [Building](#building)
- [Usage](#usage)
- [Command Reference](#command-reference)
- [How the Object Store Works](#how-the-object-store-works)
- [Limitations](#limitations)

---

## Features

- `init` — initialise a repository with an interactive strategy selector
- `add` — stage files and directories into the index
- `commit` — create commits using one of three storage strategies
- `branch` — create, list, and delete branches
- `checkout` — switch branches and restore working tree
- `log` — walk and display the commit history
- `status` — three-way diff between committed tree, index, and working directory
- `lifetime` — file lifetime tracker: size over time, churn rate, ASCII bar chart

---

## Architecture

Mini-VCS follows Git's on-disk layout exactly. Every piece of content — file data, directory snapshots, commit metadata — is stored as a **content-addressed object** under `.git/objects/`. The SHA-1 hash of an object's content is its identifier and its storage key.

```
.git/
├── HEAD                  ← "ref: refs/heads/master" (current branch pointer)
├── config                ← repository config (stores chosen commit strategy)
├── index                 ← staging area: path → blob-hash map
├── objects/              ← content-addressed object store
│   └── <2-char-prefix>/
│       └── <38-char-suffix>   ← zlib-compressed object file
└── refs/
    └── heads/
        └── <branch-name>      ← one file per branch, contains commit hash
```

### Object types

| Type | Content | Identified by |
|------|---------|---------------|
| **Blob** | Raw file bytes | SHA-1 of `"blob <size>\0<content>"` |
| **Tree** | Sorted list of `(mode, name, hash)` entries | SHA-1 of encoded entries |
| **Commit** | `tree`, `parent`, `author`, `timestamp`, `message` | SHA-1 of commit text |

Every object is stored as `zlib(header + content)` — identical to real Git. If two files have the same content, they share one blob object automatically.

### Module layout

```
main.cpp              ← CLI argument parsing, command dispatch only
includes/
  utils.h             ← hex/binary, file I/O, zlib, SHA-1, string helpers
  objects.h           ← GitObject, Blob, Tree, CommitData
  index.h             ← staging area load/save
  repository.h        ← Repository class: paths + shared object store ops
  commit_strategy.h   ← CommitStrategy enum + CommitResult struct
  repo_config.h       ← read/write .git/config (stores chosen strategy)
  cmd_*.h             ← one header per command
src/
  utils.cpp           ← implementations of utils
  objects.cpp         ← GitObject serialise/deserialise, Tree parsing
  index.cpp           ← index file read/write
  repository.cpp      ← object store, tree helpers, branch/HEAD ops
  commit_strategy.cpp ← strategy enum helpers, result pretty-printer
  repo_config.cpp     ← config file I/O
  cmd_init.cpp        ← `init` with interactive strategy prompt
  cmd_add.cpp         ← `add`: hash files, write blobs, update index
  cmd_commit.cpp      ← `commit`: three strategy implementations
  cmd_checkout.cpp    ← `checkout`: restore working tree from commit tree
  cmd_branch.cpp      ← `branch`: list / create / delete
  cmd_log.cpp         ← `log`: walk parent chain, pretty-print
  cmd_status.cpp      ← `status`: three-way diff
  cmd_lifetime.cpp    ← `lifetime`: file size history + bar chart
```

---

## Commit Strategies

Chosen once at `init` time and stored in `.git/config`. Every `commit` uses the chosen strategy for that repository.

### Strategy 1 — FULL_COPY

Every staged file gets a new blob object written to the object store on every commit, regardless of whether its content changed.

- **Storage cost**: O(files × commits) — grows with every commit
- **Commit speed**: Fast — just hash and store
- **Correctness**: Perfect — no deduplication logic to get wrong
- **Best for**: Small repositories, academic/demo use, when simplicity matters

### Strategy 2 — DELTA_METADATA

Before writing a blob, the strategy reads the file's `mtime` (modification time) and size from the filesystem and compares them against a metadata cache from the previous commit. If both values are unchanged, the file is assumed unmodified and a **sentinel blob** is stored instead of the real content. A sentinel blob contains the string `ref:<prev_commit_hash>` — a pointer, not data.

- **Storage cost**: O(changed files per commit) — unchanged files cost ~50 bytes each
- **Commit speed**: Fastest — only a `stat()` call per file, no reads
- **Correctness**: Good, but can miss a silent in-place overwrite where the new content happens to be the same byte count and the write landed in the same second
- **Best for**: Large repos with many large files where commit speed matters

### Strategy 3 — DELTA_HASH

Before writing a blob, the strategy re-reads and SHA-1 hashes the current file content, then looks up the hash of the same file from the previous commit's tree. If the hashes match, content is identical and a sentinel blob is stored. If they differ, a new blob is written.

- **Storage cost**: O(changed files per commit) — same as Strategy 2
- **Commit speed**: Slightly slower — must read and hash every staged file
- **Correctness**: Perfect — catches every real change, including same-size silent overwrites that Strategy 2 misses
- **Best for**: Production use; this is essentially what real Git does

### Sentinel blob resolution

Checkout and the `lifetime` tracker both resolve sentinel blobs transparently. When a `ref:<hash>` blob is encountered, the code follows the chain: load the referenced commit → look up the file in its tree → repeat until a real blob is found (capped at 64 hops to prevent cycles). Users never see sentinel content — they always get the correct file data.

### Storage comparison (same 3-file project, 5 commits)

```
Strategy 1 (FULL_COPY):       ~15 blob objects
Strategy 2 (DELTA_METADATA):  ~6 blob objects + 9 sentinel blobs
Strategy 3 (DELTA_HASH):      ~6 blob objects + 9 sentinel blobs
```

Sentinels are small (< 50 bytes each), so Strategies 2 and 3 use significantly less disk space for projects where most files don't change on every commit.

---

## File Lifetime Tracker

```
mini_vcs lifetime <filename>
```

Walks the entire commit history of the current branch and produces a complete size history for a single file. This goes beyond what `git log -- filename` offers — it shows storage size at every commit (not just the commits that touched the file), byte-level deltas, an ASCII bar chart, and summary statistics.

### Example output

```
File Lifetime Report: main.c
----------------------------------------------------------
Commit    Date          Size      Change    Message
----------------------------------------------------------
a1b2c3d   Apr 01 10:37  82B       +82B      Initial skeleton
e4f5a6b   Apr 01 11:02  241B      +159B     Add greet and add()
d7e8f9c   Apr 01 11:30  241B        --      Changed config only
b1c2d3e   Apr 01 12:05  748B      +507B     Add Player struct
f4a5b6c   Apr 01 12:41  67B       -681B     Refactor: strip to min…
----------------------------------------------------------

Size over time (each # = 18 bytes):
      82B  |####
     241B  |#############
     241B  |#############
     748B  |########################################
      67B  |####

----------------------------------------------------------
Total lifetime  : 5 commits
Times changed   : 4 out of 5 commits (80% churn rate)
Net growth      : -15B from first to last version
Peak size       : 748B at commit b1c2d3e
----------------------------------------------------------
```

### What each section tells you

- **Table**: Every commit on the branch, whether or not it touched this file. `--` in the Change column means the file existed but its size didn't change. `(absent)` means the file wasn't in that commit's tree yet (introduced mid-history or deleted).
- **Bar chart**: Visual encoding of file growth over time. Each `#` represents N bytes (scaled to fit the terminal). Peaks show complexity accumulation; drops show refactors.
- **Summary**: Churn rate (percentage of commits where size changed), net growth from first to last version, and peak size with the commit it occurred at.

### Research connection

This feature applies **Lehman's Laws of Software Evolution** (Lehman & Belady, 1974) at the individual file level:

- **Law I — Continuing Change**: Files in active use must be continually adapted. The churn rate metric quantifies this directly.
- **Law VI — Continuing Growth**: Functional content must be continually increased. The size-over-time chart makes this visible per file, not just at the system level.

Git has no equivalent command. `git log -- filename` shows commits but not sizes. `git cat-file -s <hash>` gives one blob's size but requires you to know the hash. `lifetime` gives you the full picture in one command.

---

## Project Structure

```
Mini_VCS/
├── Makefile
├── README.md
├── main.cpp
├── includes/
│   ├── utils.h
│   ├── objects.h
│   ├── index.h
│   ├── repository.h
│   ├── commit_strategy.h
│   ├── repo_config.h
│   ├── cmd_init.h
│   ├── cmd_add.h
│   ├── cmd_commit.h
│   ├── cmd_checkout.h
│   ├── cmd_branch.h
│   ├── cmd_log.h
│   ├── cmd_status.h
│   └── cmd_lifetime.h
└── src/
    ├── utils.cpp
    ├── objects.cpp
    ├── index.cpp
    ├── repository.cpp
    ├── commit_strategy.cpp
    ├── repo_config.cpp
    ├── cmd_init.cpp
    ├── cmd_add.cpp
    ├── cmd_commit.cpp
    ├── cmd_checkout.cpp
    ├── cmd_branch.cpp
    ├── cmd_log.cpp
    ├── cmd_status.cpp
    └── cmd_lifetime.cpp
```

---

## Dependencies

| Library | Used for | macOS | Linux |
|---------|----------|-------|-------|
| **zlib** | Object compression (same format as Git) | Ships with Xcode CLT | `apt install zlib1g-dev` |
| **OpenSSL (libcrypto)** | SHA-1 hashing via `SHA1()` | Ships with Xcode CLT | `apt install libssl-dev` |
| **C++17 STL** | `std::filesystem`, `std::optional`, etc. | g++/clang++ ≥ 7 | g++ ≥ 7 |

No other third-party libraries are required.

---

## Building

### macOS

```bash
# Install Xcode command line tools if not already present
xcode-select --install

# Clone or unzip the project
cd Mini_VCS

# Build
make

# The binary will be at ./mini_vcs
./mini_vcs
```

### Linux (Ubuntu / Debian)

```bash
sudo apt install build-essential libssl-dev zlib1g-dev
cd Mini_VCS
make
./mini_vcs
```

### Manual build (without Make)

```bash
g++ -std=c++17 -O2 -Iincludes \
    src/utils.cpp src/objects.cpp src/index.cpp src/repository.cpp \
    src/commit_strategy.cpp src/repo_config.cpp \
    src/cmd_init.cpp src/cmd_add.cpp src/cmd_commit.cpp \
    src/cmd_checkout.cpp src/cmd_branch.cpp \
    src/cmd_log.cpp src/cmd_status.cpp src/cmd_lifetime.cpp \
    main.cpp \
    -lz -lcrypto -o mini_vcs
```

### Add to PATH (optional, macOS/Linux)

```bash
# Temporary (current session only)
export PATH="$PATH:/path/to/Mini_VCS"

# Permanent (add to shell config)
echo 'export PATH="$PATH:/path/to/Mini_VCS"' >> ~/.zshrc
source ~/.zshrc
```

---

## Usage

### Quick start

```bash
# Create a new repo (prompts for strategy choice)
mkdir myproject && cd myproject
mini_vcs init

# Create some files, stage them, commit
echo "hello" > hello.txt
mini_vcs add hello.txt
mini_vcs commit -m "First commit"

# Check what's staged / modified
mini_vcs status

# View history
mini_vcs log

# See the full lifetime of a file
mini_vcs lifetime hello.txt
```

### Working with branches

```bash
# List all branches
mini_vcs branch

# Create a new branch
mini_vcs branch feature/new-thing

# Switch to it
mini_vcs checkout feature/new-thing

# Create and switch in one step
mini_vcs checkout -b feature/another-thing

# Delete a branch
mini_vcs branch feature/old-thing -d
```

### Comparing strategies — recommended demo setup

```bash
mkdir demo && cd demo

# Three repos, same project, different strategies
mkdir repo_s1 repo_s2 repo_s3

cd repo_s1 && mini_vcs init --strategy 1 && cd ..
cd repo_s2 && mini_vcs init --strategy 2 && cd ..
cd repo_s3 && mini_vcs init --strategy 3 && cd ..

# Make the same commits in each, then compare:
du -sh repo_s1/.git/objects repo_s2/.git/objects repo_s3/.git/objects
find repo_s1/.git/objects -type f | wc -l
find repo_s2/.git/objects -type f | wc -l
find repo_s3/.git/objects -type f | wc -l
```

---

## Command Reference

### `mini_vcs init`

Initialises a new repository in the current directory. Creates `.git/` with the object store, HEAD, index, and config. Prompts interactively for a commit strategy unless `--strategy` is passed.

```bash
mini_vcs init                  # interactive strategy prompt
mini_vcs init --strategy 1     # FULL_COPY, no prompt
mini_vcs init --strategy 2     # DELTA_METADATA, no prompt
mini_vcs init --strategy 3     # DELTA_HASH, no prompt
mini_vcs init -s 3             # short flag
```

---

### `mini_vcs add`

Hashes each given file, writes a blob object to the object store, and records the `path → blob-hash` mapping in the index. Accepts multiple files and directories (directories are staged recursively).

```bash
mini_vcs add main.c
mini_vcs add src/
mini_vcs add main.c config.txt README.md
```

---

### `mini_vcs commit`

Creates a commit from whatever is currently in the index. Builds a tree object hierarchy from the index, creates a commit object pointing to that tree and the previous commit, writes it to the object store, and advances the current branch pointer.

The `--author` flag is optional; a default is used if omitted.

```bash
mini_vcs commit -m "Fix null pointer in parser"
mini_vcs commit -m "Release v1.0" --author "Alice <alice@example.com>"
```

The commit output shows which strategy was used, how long it took, how many bytes were stored, and how many files were referenced (sentinel) vs newly stored:

```
── Commit Stats (DELTA_HASH) ──────────────────
  Commit hash  : 4a7f3b2e...
  Time taken   : 3 ms
  Storage used : 142 B  (1 file(s) stored, 2 file(s) referenced)
────────────────────────────────────────────────
```

---

### `mini_vcs log`

Walks the parent chain from `HEAD` and prints commit metadata. Defaults to 10 commits; use `-n` for more or fewer.

```bash
mini_vcs log
mini_vcs log -n 5
mini_vcs log -n 100
```

---

### `mini_vcs status`

Computes a three-way comparison:
1. Files in the last commit's tree
2. Files in the current index (staging area)
3. Files on disk in the working directory

```bash
mini_vcs status
```

Example output:
```
On branch master
Changes staged for commit:
  modified:   main.c

Changes not staged for commit:
  (none)

Untracked files:
  notes.txt
```

---

### `mini_vcs branch`

```bash
mini_vcs branch                   # list all branches (* = current)
mini_vcs branch feature/login     # create new branch at current HEAD
mini_vcs branch feature/login -d  # delete branch
```

Branches are stored as plain text files in `.git/refs/heads/`. Creating a branch is O(1) — it just writes one 40-character commit hash to a file.

---

### `mini_vcs checkout`

Switches to a different branch. Removes all files tracked by the current branch from the working tree, then restores all files from the target branch's latest commit tree. Sentinel blobs are resolved transparently during restore.

```bash
mini_vcs checkout main
mini_vcs checkout -b feature/experimental   # create + switch
```

---

### `mini_vcs lifetime`

Shows the complete size history of a single file across all commits on the current branch. Resolves sentinel blobs to report actual content sizes even under Strategies 2 and 3.

```bash
mini_vcs lifetime main.c
mini_vcs lifetime src/utils.cpp    # works for files in subdirectories
mini_vcs lifetime nonexistent.h   # gracefully shows all rows as (absent)
```

---

## How the Object Store Works

Every object goes through the same pipeline:

```
raw content
    │
    ├─ prepend header: "<type> <length>\0"
    │
    ├─ SHA-1 hash the whole thing → 40-char hex → object identity
    │
    ├─ zlib compress
    │
    └─ write to .git/objects/<first-2-chars>/<remaining-38-chars>
```

This is byte-for-byte compatible with Git's object format. You can inspect Mini-VCS objects with Git tools:

```bash
# Inside a Mini-VCS repo
git cat-file -t <hash>       # prints "blob", "tree", or "commit"
git cat-file -p <hash>       # prints the object's content
```

The two-level directory structure (`xx/yyyyyy...`) limits each subdirectory to at most 256 entries on average — the same fan-out trick Git uses to avoid filesystem slowdowns on directories with thousands of files.

---

## Limitations

- **Single-parent commits only** — merge commits are not supported. The parent chain is always linear.
- **No remote operations** — there is no `push`, `pull`, `fetch`, or network layer.
- **No diff output** — `status` shows which files changed but not what changed inside them. There is no `diff` command.
- **No `reset` or `revert`** — you cannot undo a commit or unstage files through the CLI (though the object store is intact and a future implementation could add this).
- **First-parent history only** in `log` and `lifetime` — sufficient for a linear workflow.
- **SHA-1** — same as pre-2020 Git. Vulnerable to the SHAttered collision in adversarial contexts, but appropriate for academic/local use.
- **Strategy is fixed at init time** — you cannot change a repository's strategy after initialization without re-creating it.