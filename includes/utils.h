#pragma once
/**
 * utils.h
 * -------
 * Shared low-level utility functions used across all modules:
 *   - Hex / binary conversions
 *   - File I/O helpers
 *   - zlib compress / decompress
 *   - SHA-1 hashing
 *   - String helpers (trim, split)
 */

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/sha.h>
#include <zlib.h>

namespace fs = std::filesystem;

// ── Hex / Binary ──────────────────────────────────────────────────────────────

/** Raw bytes → lowercase hex string. */
std::string to_hex(const unsigned char* data, size_t len);

/** 20-byte binary SHA-1 digest → 40-char hex string. */
std::string bin_to_hex(const std::string& bin);

/** 40-char hex string → 20-byte binary. */
std::string hex_to_bin(const std::string& hex);

// ── File I/O ──────────────────────────────────────────────────────────────────

/** Read entire file into a string (binary-safe). */
std::string read_file(const fs::path& p);

/** Write string to file, creating parent directories as needed. */
void write_file(const fs::path& p, const std::string& data);

// ── Compression ───────────────────────────────────────────────────────────────

/** zlib-compress a byte string. */
std::string zlib_compress(const std::string& input);

/** zlib-decompress a byte string. */
std::string zlib_decompress(const std::string& input);

// ── Hashing ───────────────────────────────────────────────────────────────────

/** SHA-1 of an arbitrary byte string → 40-char hex. */
std::string sha1_hex(const std::string& data);

// ── String helpers ────────────────────────────────────────────────────────────

/** Strip trailing whitespace and newlines. */
std::string trim(const std::string& s);

/** Split string by a single-character delimiter. */
std::vector<std::string> split(const std::string& s, char delim);
