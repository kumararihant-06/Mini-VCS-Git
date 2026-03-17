/**
 * objects.cpp
 * -----------
 * Implementations of all Git object types declared in objects.h.
 */

#include "objects.h"
#include "utils.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

// ── GitObject ─────────────────────────────────────────────────────────────────

GitObject::GitObject(std::string t, std::string c)
    : type(std::move(t)), content(std::move(c)) {}

std::string GitObject::hash() const {
    std::string hdr = type + " " + std::to_string(content.size()) + '\0';
    return sha1_hex(hdr + content);
}

std::string GitObject::serialize() const {
    std::string hdr = type + " " + std::to_string(content.size()) + '\0';
    return zlib_compress(hdr + content);
}

GitObject GitObject::deserialize(const std::string& data) {
    std::string raw = zlib_decompress(data);
    size_t null_pos = raw.find('\0');
    if (null_pos == std::string::npos)
        throw std::runtime_error("Malformed object: no null byte");
    std::string header  = raw.substr(0, null_pos);
    std::string payload = raw.substr(null_pos + 1);
    auto space = header.find(' ');
    if (space == std::string::npos)
        throw std::runtime_error("Malformed object header");
    return {header.substr(0, space), payload};
}

// ── Blob ──────────────────────────────────────────────────────────────────────

Blob::Blob(const std::string& bytes) : GitObject("blob", bytes) {}

// ── Tree ──────────────────────────────────────────────────────────────────────

Tree::Tree() : GitObject("tree", "") {}

void Tree::add_entry(const std::string& mode,
                     const std::string& name,
                     const std::string& hash_hex) {
    entries.push_back({mode, name, hash_hex});
    content = serialize_entries();
}

std::string Tree::serialize_entries() const {
    // Git sorts tree entries lexicographically by name
    std::vector<TreeEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const TreeEntry& a, const TreeEntry& b) { return a.name < b.name; });

    std::string out;
    for (const auto& e : sorted) {
        out += e.mode + " " + e.name + '\0';
        out += hex_to_bin(e.obj_hash); // 20 raw bytes
    }
    return out;
}

Tree Tree::from_content(const std::string& raw) {
    Tree t;
    size_t i = 0;
    while (i < raw.size()) {
        size_t null_pos = raw.find('\0', i);
        if (null_pos == std::string::npos) break;
        std::string mode_name = raw.substr(i, null_pos - i);
        auto sp = mode_name.find(' ');
        if (sp == std::string::npos) break;
        std::string mode = mode_name.substr(0, sp);
        std::string name = mode_name.substr(sp + 1);
        if (null_pos + 20 >= raw.size() + 1) break; // safety check
        std::string bin_hash = raw.substr(null_pos + 1, 20);
        t.entries.push_back({mode, name, bin_to_hex(bin_hash)});
        i = null_pos + 21;
    }
    return t;
}

// ── CommitData ────────────────────────────────────────────────────────────────

GitObject CommitData::to_object() const {
    std::string body = "tree " + tree_hash + "\n";
    for (const auto& p : parent_hashes)
        body += "parent " + p + "\n";
    body += "author "    + author    + " " + std::to_string(timestamp) + " +0000\n";
    body += "committer " + committer + " " + std::to_string(timestamp) + " +0000\n";
    body += "\n" + message;
    return {"commit", body};
}

CommitData CommitData::from_content(const std::string& raw) {
    CommitData c;
    std::istringstream ss(raw);
    std::string line;
    bool        in_body = false;
    std::string body;

    while (std::getline(ss, line)) {
        if (in_body) { body += line + "\n"; continue; }
        if (line.empty()) { in_body = true; continue; }

        if (line.rfind("tree ", 0) == 0) {
            c.tree_hash = line.substr(5);
        } else if (line.rfind("parent ", 0) == 0) {
            c.parent_hashes.push_back(line.substr(7));
        } else if (line.rfind("author ", 0) == 0) {
            // format: "author Name <email> TIMESTAMP +0000"
            auto parts = split(line.substr(7), ' ');
            if (parts.size() >= 2) {
                c.timestamp = std::stoll(parts[parts.size() - 2]);
                std::string a;
                for (size_t k = 0; k + 2 < parts.size(); ++k)
                    a += (k ? " " : "") + parts[k];
                c.author = a;
            }
        } else if (line.rfind("committer ", 0) == 0) {
            auto parts = split(line.substr(10), ' ');
            std::string cm;
            for (size_t k = 0; k + 2 < parts.size(); ++k)
                cm += (k ? " " : "") + parts[k];
            c.committer = cm;
        }
    }
    // strip trailing newline from message body
    while (!body.empty() && body.back() == '\n') body.pop_back();
    c.message = body;
    return c;
}
