// WS7 (docs/wx-server.md): the device's WXC1 decoder (include/WxCrop.h) against crops the SERVER PATH made
// -- proxy/src/wx/handler.ts over a synthetic WXG1 in an in-memory R2, written by
// proxy/scripts/wx-fixtures.ts (workers.yml fails if they are stale).
//
//   test_wx_server_crops.exe <repo>/test/fixtures/wx/server
//
// Two lines per crop, so a sabotage can show which check caught it:
//   file    the file's sha256 equals the manifest's (the fixture is the one the generator wrote)
//   cells   it decodes Ok, its header matches, and its cells' sha256 equals the ENCODER's own cells
// Exit: 0 ok, 1 a check failed, 2 the rig is broken (manifest unreadable, or not exactly 8 crops).
// Its sabotage: python -I test/host/sabotage_wx_server_crops.py
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "WxCrop.h"

typedef std::vector<uint8_t> Bytes;
static int failures = 0;
static void check(bool ok, const std::string& name, const char* id, const std::string& what)
{
    std::printf("  %s  %-5s %s: %s\n", ok ? "ok  " : "FAIL", id, name.c_str(), what.c_str());
    if (!ok) ++failures;
}

// ---- SHA-256 (FIPS 180-4): the same implementation as test_wx_crop.cpp ----------------------------------
namespace sha {
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
static uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static std::string Hex(const uint8_t* data, size_t len)
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    Bytes m(data, data + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    const uint64_t bits = (uint64_t)len * 8;
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)m[off + 4 * i] << 24) | ((uint32_t)m[off + 4 * i + 1] << 16) |
                   ((uint32_t)m[off + 4 * i + 2] << 8) | m[off + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = k + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return std::string(out, 64);
}
}  // namespace sha

static std::string g_dir;
static bool ReadFile(const std::string& name, Bytes* out)
{
    FILE* f = std::fopen((g_dir + "/" + name).c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

// One crop's object in the manifest: from its "name" up to the next one.
static std::string Object(const std::string& j, size_t at)
{
    const size_t next = j.find("\"name\": \"", at + 1);
    return j.substr(at, next == std::string::npos ? std::string::npos : next - at);
}
static std::string Str(const std::string& o, const char* key)
{
    const std::string pat = std::string("\"") + key + "\": \"";
    const size_t p = o.find(pat);
    if (p == std::string::npos) return "";
    const size_t s = p + pat.size();
    return o.substr(s, o.find('"', s) - s);
}
static long Num(const std::string& o, const char* key)
{
    const std::string pat = std::string("\"") + key + "\": ";
    const size_t p = o.find(pat);
    if (p == std::string::npos) return -1;
    const std::string v = o.substr(p + pat.size(), 8);
    if (v.compare(0, 4, "true") == 0) return 1;
    if (v.compare(0, 5, "false") == 0) return 0;
    return std::strtol(v.c_str(), nullptr, 10);
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_server_crops <repo>/test/fixtures/wx/server\n"); return 2; }
    g_dir = argv[1];
    std::printf("WXC1 decoder (WxCrop.h) vs crops the server path made (proxy/src/wx/handler.ts)\n");
    Bytes manB;
    if (!ReadFile("MANIFEST.json", &manB)) { std::printf("FAIL: cannot read %s/MANIFEST.json (rig)\n", g_dir.c_str()); return 2; }
    const std::string man(manB.begin(), manB.end());
    std::vector<size_t> at;
    for (size_t p = man.find("\"name\": \""); p != std::string::npos; p = man.find("\"name\": \"", p + 1)) at.push_back(p);
    if (at.size() != 8) { std::printf("FAIL: the manifest lists %u crops, not 8 (rig)\n", (unsigned)at.size()); return 2; }

    static uint8_t buf[405 * 405];   // the widest crop the server can make (docs/wx-server.md)
    int good = 0;
    for (size_t i = 0; i < at.size(); ++i) {
        const std::string o = Object(man, at[i]), name = Str(o, "name");
        Bytes b;
        if (!ReadFile(name, &b)) { check(false, name, "file", "unreadable"); continue; }
        const std::string fileSha = sha::Hex(b.data(), b.size());
        check(fileSha == Str(o, "sha256"), name, "file", "sha256 " + fileSha.substr(0, 12) + (fileSha == Str(o, "sha256") ? " == manifest" : " != manifest"));
        wxcrop::Header h;
        const wxcrop::Status s = wxcrop::DecodeInto(b.data(), b.size(), buf, sizeof buf, &h);
        bool ok = s == wxcrop::Status::Ok;
        std::string what = std::string("decode ") + wxcrop::StatusName(s);
        if (ok) {
            const bool hdr = h.coarsen == Num(o, "coarsen") && (long)h.Coarsened() == Num(o, "flagged") &&
                             h.rows == Num(o, "rows") && h.cols == Num(o, "cols");
            const std::string cells = sha::Hex(buf, h.Cells());
            ok = hdr && cells == Str(o, "cellsSha256");
            char d[96];
            std::snprintf(d, sizeof d, ", %ux%u k=%u%s, cells sha256 %s", h.rows, h.cols, h.coarsen, h.Coarsened() ? " flagged" : "", cells.substr(0, 12).c_str());
            what += d;
            what += hdr ? "" : " (HEADER DIFFERS from the manifest)";
            what += cells == Str(o, "cellsSha256") ? " == the encoder's" : " != the encoder's";
        }
        check(ok, name, "cells", what);
        if (ok) ++good;
    }
    std::printf("  ->  %d / %u crops decoded to the encoder's own cells (predicted 8 / 8)\n", good, (unsigned)at.size());
    std::printf(failures ? "WX SERVER CROP TESTS FAILED\n" : "all WX server crop checks passed\n");
    return failures ? 1 : 0;
}
