/**
 * utils.cpp
 * ---------
 * Implementations of shared utility helpers declared in utils.h.
 */

#include "utils.h"

#include <stdexcept>
#include <string>

// ── Hex / Binary ──────────────────────────────────────────────────────────────

std::string to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return oss.str();
}

std::string bin_to_hex(const std::string& bin) {
    return to_hex(reinterpret_cast<const unsigned char*>(bin.data()), bin.size());
}

std::string hex_to_bin(const std::string& hex) {
    assert(hex.size() == 40);
    std::string bin;
    bin.reserve(20);
    for (size_t i = 0; i < 40; i += 2) {
        unsigned char byte = (unsigned char)std::stoi(hex.substr(i, 2), nullptr, 16);
        bin.push_back((char)byte);
    }
    return bin;
}

// ── File I/O ──────────────────────────────────────────────────────────────────

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}

void write_file(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot write file: " + p.string());
    f.write(data.data(), (std::streamsize)data.size());
}

// ── Compression ───────────────────────────────────────────────────────────────

std::string zlib_compress(const std::string& input) {
    uLongf out_len = compressBound((uLong)input.size());
    std::string out(out_len, '\0');
    if (compress(reinterpret_cast<Bytef*>(out.data()), &out_len,
                 reinterpret_cast<const Bytef*>(input.data()),
                 (uLong)input.size()) != Z_OK)
        throw std::runtime_error("zlib compress failed");
    out.resize(out_len);
    return out;
}

std::string zlib_decompress(const std::string& input) {
    std::string out;
    out.resize(input.size() * 4);
    for (int attempt = 0; attempt < 8; ++attempt) {
        uLongf out_len = (uLongf)out.size();
        int rc = uncompress(reinterpret_cast<Bytef*>(out.data()), &out_len,
                            reinterpret_cast<const Bytef*>(input.data()),
                            (uLong)input.size());
        if (rc == Z_OK)        { out.resize(out_len); return out; }
        if (rc == Z_BUF_ERROR) { out.resize(out.size() * 2); continue; }
        throw std::runtime_error("zlib decompress failed");
    }
    throw std::runtime_error("zlib decompress: buffer too small");
}

// ── Hashing ───────────────────────────────────────────────────────────────────

std::string sha1_hex(const std::string& data) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
    return to_hex(digest, SHA_DIGEST_LENGTH);
}

// ── String helpers ────────────────────────────────────────────────────────────

std::string trim(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim)) parts.push_back(token);
    return parts;
}
