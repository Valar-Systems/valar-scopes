// Weatherscope 3.3c's loop crops (test/fixtures/wx/loop/) graded by the DEVICE's decoder (include/WxCrop.h) against
// the encoder's own cells in MANIFEST.json -- docs/weatherscope-stage3.md A8.10, rows D2-D4:
//   D2  each crop is <= 32,768 B and its manifest size, Validate()s Ok, and decodes to its manifest codesSha256
//   D3  all 15 share rows, cols, latNwMicro and lonNwMicro (one geometry)
//   D4  frameEpoch strictly ascending, consecutive gaps 200-280 s
// D5 (the total) is printed, not graded here: it was ruled a miss (A8.12).
//
//     test_wx_loop <test/fixtures/wx/loop>
//
// Its sabotages: python -I test/host/sabotage_wx_loop.py. Prints no coordinate.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WxCrop.h"

typedef std::vector<uint8_t> Bytes;

namespace {

int checks = 0, failures = 0;
void check(bool ok, const std::string& name, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

// SHA-256, as in test_wx_crop.cpp.
namespace sha {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
std::string Hex(const uint8_t* data, size_t len)
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

bool ReadFile(const std::string& path, Bytes* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}
std::string StringAfter(const std::string& j, size_t from, const char* key, size_t* at = nullptr)
{
    const std::string pat = std::string("\"") + key + "\": \"";
    const size_t p = j.find(pat, from);
    if (at) *at = p;
    if (p == std::string::npos) return "";
    const size_t s = p + pat.size(), e = j.find('"', s);
    return j.substr(s, e - s);
}
long NumberAfter(const std::string& j, size_t from, const char* key)
{
    const std::string pat = std::string("\"") + key + "\": ";
    const size_t p = j.find(pat, from);
    return p == std::string::npos ? -1 : std::strtol(j.c_str() + p + pat.size(), nullptr, 10);
}

struct Frame {
    std::string name, codes;
    long bytes = -1;
};

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_loop <test/fixtures/wx/loop>\n"); return 2; }
    const std::string dir = argv[1];
    Bytes mj;
    if (!ReadFile(dir + "/MANIFEST.json", &mj)) { std::printf("cannot read %s/MANIFEST.json\n", dir.c_str()); return 2; }
    const std::string j(mj.begin(), mj.end());
    std::vector<Frame> frames;
    for (size_t at = j.find("\"frames\""); at != std::string::npos;) {
        size_t p;
        const std::string name = StringAfter(j, at, "name", &p);
        if (p == std::string::npos) break;
        Frame f;
        f.name = name;
        f.codes = StringAfter(j, p, "codesSha256");
        f.bytes = NumberAfter(j, p, "bytes");
        frames.push_back(f);
        at = p + 1;
    }
    if (frames.size() != 15) { std::printf("the manifest lists %u frames, not 15\n", (unsigned)frames.size()); return 2; }

    std::printf("== D2: each crop, by the device decoder ==\n");
    std::vector<wxcrop::Header> heads(frames.size());
    long total = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        Bytes b;
        if (!ReadFile(dir + "/" + frames[i].name, &b)) { std::printf("cannot read %s\n", frames[i].name.c_str()); return 2; }
        total += (long)b.size();
        wxcrop::Header h;
        const wxcrop::Status v = wxcrop::Validate(b.data(), b.size(), &h);
        std::string got = "-";
        if (v == wxcrop::Status::Ok) {
            std::vector<uint8_t> cells(h.Cells());
            if (wxcrop::DecodeInto(b.data(), b.size(), cells.data(), cells.size()) == wxcrop::Status::Ok)
                got = sha::Hex(cells.data(), cells.size());
        } else {
            wxcrop::ParseHeader(b.data(), b.size(), &h);
        }
        heads[i] = h;
        const bool ok = b.size() <= 32768 && v == wxcrop::Status::Ok && got == frames[i].codes;
        char d[200];
        std::snprintf(d, sizeof d, "%u B (manifest %ld, not graded), Validate %s, cells %.12s (manifest %.12s)",
                      (unsigned)b.size(), frames[i].bytes, wxcrop::StatusName(v), got.c_str(), frames[i].codes.c_str());
        check(ok, "D2:" + frames[i].name.substr(0, frames[i].name.size() - 4), d);
    }

    std::printf("== D3: one geometry ==\n");
    for (size_t i = 0; i < frames.size(); ++i) {
        const bool ok = heads[i].rows == heads[0].rows && heads[i].cols == heads[0].cols &&
                        heads[i].latNwMicro == heads[0].latNwMicro && heads[i].lonNwMicro == heads[0].lonNwMicro;
        char d[96];
        std::snprintf(d, sizeof d, "%u x %u, NW corner %s frame 1's", heads[i].rows, heads[i].cols,
                      heads[i].latNwMicro == heads[0].latNwMicro && heads[i].lonNwMicro == heads[0].lonNwMicro ? "equals" : "DIFFERS FROM");
        check(ok, "D3:" + frames[i].name.substr(0, frames[i].name.size() - 4), d);
    }

    std::printf("== D4: the clock ==\n");
    for (size_t i = 1; i < frames.size(); ++i) {
        const long gap = (long)heads[i].frameEpoch - (long)heads[i - 1].frameEpoch;
        char d[64];
        std::snprintf(d, sizeof d, "gap %ld s", gap);
        check(gap >= 200 && gap <= 280, "D4:" + frames[i].name.substr(0, frames[i].name.size() - 4), d);
    }

    std::printf("D5 (ruled a miss, A8.12; printed, not graded): %ld B in all\n", total);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
