/**
 * index.cpp
 * ---------
 * Load and save the staging-area index (a flat JSON map).
 * Uses a minimal hand-rolled JSON parser/serialiser so no extra
 * dependencies are needed.
 */

#include "index.h"
#include "utils.h"

#include <sstream>
#include <stdexcept>

// ── Minimal JSON helpers (flat string→string map only) ────────────────────────

static std::string json_serialize(const std::map<std::string, std::string>& m) {
    std::ostringstream oss;
    oss << "{\n";
    size_t i = 0;
    for (const auto& [k, v] : m) {
        oss << "  \"" << k << "\": \"" << v << "\"";
        if (++i < m.size()) oss << ",";
        oss << "\n";
    }
    oss << "}";
    return oss.str();
}

static std::map<std::string, std::string> json_parse(const std::string& s) {
    std::map<std::string, std::string> m;
    size_t pos = 0;

    auto skip_ws = [&]() {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    };
    auto read_string = [&]() -> std::string {
        skip_ws();
        if (pos >= s.size() || s[pos] != '"') return "";
        ++pos;
        std::string out;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\') ++pos; // skip escape character
            out += s[pos++];
        }
        if (pos < s.size()) ++pos; // skip closing "
        return out;
    };

    while (pos < s.size()) {
        skip_ws();
        if (pos >= s.size()) break;
        char c = s[pos];
        if (c == '{' || c == '}' || c == ',') { ++pos; continue; }
        if (c == '"') {
            std::string key = read_string();
            skip_ws();
            if (pos < s.size() && s[pos] == ':') ++pos;
            std::string val = read_string();
            if (!key.empty()) m[key] = val;
        } else {
            ++pos;
        }
    }
    return m;
}

// ── Public API ────────────────────────────────────────────────────────────────

std::map<std::string, std::string> load_index(const fs::path& index_file) {
    if (!fs::exists(index_file)) return {};
    try { return json_parse(read_file(index_file)); }
    catch (...) { return {}; }
}

void save_index(const fs::path& index_file,
                const std::map<std::string, std::string>& idx) {
    write_file(index_file, json_serialize(idx));
}
