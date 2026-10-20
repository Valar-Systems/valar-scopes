// The WXC1 decoder (include/WxCrop.h) against crops the weather spike's OWN encoder wrote.
//
//   test_wx_crop.exe <repo>/test/fixtures/wx
//
// Every expected value comes from test/fixtures/wx/MANIFEST.json, where two references agree on it:
// crop.ts's decodeCrop and the independent Python decoder in a3_check.py (check_fixtures.py). Nothing
// expected is typed in here except the header fields and counts frozen in docs/weatherscope-stage1.md
// (Predictions A), which this prints against, one line per check.
//
//   A1  4 valid fixtures decode to the agreed codes sha256
//   A2  3 negative fixtures refused, each with its own reason, with no partial output
//   A3  12 mutations of the KDFW crop refused, each with its own reason, with no partial output
//       (7 frozen with the predictions, 5 added by the #429 review's addendum)
//   A4  the 50-crop corpus round-trips to the encoder's own cells
//   A5  the 50 sabotaged twins (long runs written one short) are refused
//   A6  no heap allocation during any decode (operator new, malloc, calloc, realloc counted)
//   A7  KDFW's run callback: 3,741 calls, row-major, contiguous, summing to 36,192
//   A8  KDFW and KMSP header fields
//
// Exit: 0 ok, 1 a check failed, 2 the rig is broken (fixtures unreadable).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// ---- allocation counting (A6) ------------------------------------------------------------------
static long g_allocs = 0;
static bool g_counting = false;
void* operator new(std::size_t n)
{
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n)
{
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
inline void* CountedMalloc(std::size_t n) { if (g_counting) ++g_allocs; return std::malloc(n); }
inline void* CountedCalloc(std::size_t a, std::size_t b) { if (g_counting) ++g_allocs; return std::calloc(a, b); }
inline void* CountedRealloc(void* p, std::size_t n) { if (g_counting) ++g_allocs; return std::realloc(p, n); }
// Every system header the decoder uses is already included above, so these only reach WxCrop.h's own code.
#define malloc(n) CountedMalloc(n)
#define calloc(a, b) CountedCalloc(a, b)
#define realloc(p, n) CountedRealloc(p, n)
#include "WxCrop.h"
#undef malloc
#undef calloc
#undef realloc

using wxcrop::Status;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    std::printf("  %s  %-3s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
    if (!ok) ++failures;
}

// ---- SHA-256 (FIPS 180-4), for comparing decoded codes with the manifest ------------------------
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

// ---- fixtures and the manifest ------------------------------------------------------------------
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
// The string value of the first `"key": "..."` at or after `from`, or "" (and npos) if there is none.
static std::string StringAfter(const std::string& j, size_t from, const char* key, size_t* at = nullptr)
{
    const std::string pat = std::string("\"") + key + "\": \"";
    const size_t p = j.find(pat, from);
    if (at) *at = p;
    if (p == std::string::npos) return "";
    const size_t s = p + pat.size(), e = j.find('"', s);
    return j.substr(s, e - s);
}
// A fixture's expected codes sha256 from the manifest; "" when the manifest expects a refusal.
static std::string ExpectedSha(const std::string& j, const std::string& name)
{
    const size_t n = j.find("\"name\": \"" + name + "\"");
    if (n == std::string::npos) return "!missing";
    const size_t next = j.find("\"name\": \"", n + 1);
    size_t at;
    const std::string v = StringAfter(j, n, "codesSha256", &at);
    return (at != std::string::npos && (next == std::string::npos || at < next)) ? v : "";
}

// Decode, counting allocations; returns the status and (when Ok) the codes.
static Status Decode(const Bytes& b, Bytes* codes, wxcrop::Header* h = nullptr)
{
    codes->assign(300 * 460, 0xEE);   // larger than any fixture; 0xEE is not a code
    const Bytes before = *codes;
    g_counting = true;
    const Status s = wxcrop::DecodeInto(b.data(), b.size(), codes->data(), codes->size(), h);
    g_counting = false;
    if (s != Status::Ok) {
        if (*codes != before) return Status::BufferTooSmall;   // flags a partial write as a failure below
        codes->clear();
        return s;
    }
    wxcrop::Header hh;
    wxcrop::ParseHeader(b.data(), b.size(), &hh);
    codes->resize(hh.Cells());
    return s;
}
// A refusal must also leave no trace: no callback, buffer untouched (checked inside Decode()).
static Status Refusal(const Bytes& b)
{
    Bytes codes;
    const Status s = Decode(b, &codes);
    long calls = 0;
    g_counting = true;
    const Status s2 = wxcrop::ForEachRun(b.data(), b.size(), [&](uint16_t, uint16_t, uint32_t, uint8_t) { ++calls; });
    g_counting = false;
    if (s2 != s || calls != 0) return Status::Ok;   // inconsistent or partial: reported as "not refused"
    return s;
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_crop <repo>/test/fixtures/wx\n"); return 2; }
    g_dir = argv[1];
    std::printf("WXC1 decoder (WxCrop.h) vs the spike encoder's crops\n");
    Bytes man;
    if (!ReadFile("MANIFEST.json", &man)) { std::printf("FAIL: cannot read %s/MANIFEST.json (rig)\n", g_dir.c_str()); return 2; }
    const std::string j(man.begin(), man.end());

    // ---- A1: valid fixtures -------------------------------------------------------------------
    const char* valid[] = {"kdfw-60mi.wxc", "kphx-40km-uniform.wxc", "phnl-60mi-outside-grid.wxc", "kmsp-222km-coarsened.wxc"};
    int a1 = 0;
    for (const char* name : valid) {
        Bytes b, codes;
        if (!ReadFile(name, &b)) { std::printf("FAIL: cannot read %s (rig)\n", name); return 2; }
        const std::string want = ExpectedSha(j, name);
        const Status s = Decode(b, &codes);
        const std::string got = s == Status::Ok ? sha::Hex(codes.data(), codes.size()) : std::string("refused: ") + wxcrop::StatusName(s);
        const bool ok = !want.empty() && got == want;
        a1 += ok;
        check(ok, "A1", std::string(name) + ": " + (ok ? "codes sha256 " + got.substr(0, 12) + " == both references" : "got " + got.substr(0, 24) + ", want " + want.substr(0, 12)));
    }
    std::printf("  ->  A1 %d/4 (predicted 4/4)\n", a1);

    // ---- A2: negative fixtures ----------------------------------------------------------------
    struct Neg { const char* name; Status want; };
    const Neg negs[] = {{"neg-bad-magic.wxc", Status::BadMagic}, {"neg-run-short.wxc", Status::Underrun},
                        {"neg-truncated-leb128.wxc", Status::TruncatedLength}};
    int a2 = 0;
    for (const Neg& n : negs) {
        Bytes b;
        if (!ReadFile(n.name, &b)) { std::printf("FAIL: cannot read %s (rig)\n", n.name); return 2; }
        if (!ExpectedSha(j, n.name).empty()) { std::printf("FAIL: the manifest does not expect %s to be refused (rig)\n", n.name); return 2; }
        const Status s = Refusal(b);
        a2 += s == n.want;
        check(s == n.want, "A2", std::string(n.name) + ": " + wxcrop::StatusName(s) + " (want " + wxcrop::StatusName(n.want) + ", nothing emitted)");
    }
    std::printf("  ->  A2 %d/3 refused with their reasons (predicted 3/3)\n", a2);

    // ---- A3: mutations of the KDFW crop -------------------------------------------------------
    Bytes kdfw;
    ReadFile("kdfw-60mi.wxc", &kdfw);
    int a3 = 0;
    auto mut = [&](const char* what, const Bytes& b, Status want) {
        const Status s = Refusal(b);
        a3 += s == want;
        check(s == want, "A3", std::string(what) + ": " + wxcrop::StatusName(s) + " (want " + wxcrop::StatusName(want) + ")");
    };
    { Bytes b = kdfw; b[4] = 2; mut("version 2", b, Status::BadVersion); }
    { Bytes b = kdfw; b[5] = 3; mut("coarsen 3", b, Status::BadCoarsen); }
    { Bytes b = kdfw; b[28] = b[29] = 0; mut("rows 0", b, Status::ZeroRows); }
    { Bytes b = kdfw; b[30] = b[31] = 0; mut("cols 0", b, Status::ZeroCols); }
    { Bytes b = kdfw; b.resize(32769, 0); mut("input 32,769 bytes", b, Status::TooLong); }
    {   // one short run made one cell longer: the runs now cover rows x cols + 1
        Bytes b = kdfw;
        size_t i = 32;
        while (i < b.size()) {
            if ((b[i] & 15) != 0 && (b[i] & 15) < 15) { ++b[i]; break; }
            if ((b[i] & 15) == 0) { ++i; while (b[i] & 0x80) ++i; }
            ++i;
        }
        mut("one run 1 cell longer (overrun)", b, Status::Overrun);
    }
    {   // the first long run's length re-encoded in 6 LEB128 bytes (same value, redundant continuations)
        Bytes b(kdfw.begin(), kdfw.begin() + 32);
        size_t i = 32;
        bool done = false;
        while (i < kdfw.size()) {
            const uint8_t r = kdfw[i++];
            b.push_back(r);
            if ((r & 15) != 0) continue;
            size_t k = i;
            uint32_t v = 0;
            int sh = 0;
            for (;;) { const uint8_t x = kdfw[k++]; v |= (uint32_t)(x & 0x7f) << sh; sh += 7; if (!(x & 0x80)) break; }
            if (!done) {
                for (int q = 0; q < 5; ++q) b.push_back((uint8_t)(((v >> (7 * q)) & 0x7f) | 0x80));
                b.push_back(0);
                done = true;
            } else {
                b.insert(b.end(), kdfw.begin() + i, kdfw.begin() + k);
            }
            i = k;
        }
        mut("a 6-byte LEB128 length", b, Status::LengthTooLong);
    }
    // The #429 review's three (docs/weatherscope-stage1.md, "Predictions A, addendum").
    { Bytes b = kdfw; b.push_back(0x00); b.push_back(0x00); mut("a zero-length long run appended (00 00)", b, Status::ZeroRun); }
    { Bytes b = kdfw; b[20] = b[21] = b[22] = b[23] = 0; mut("cell height 0", b, Status::ZeroCellHeight); }
    { Bytes b = kdfw; b[24] = b[25] = b[26] = b[27] = 0; mut("cell width 0", b, Status::ZeroCellWidth); }
    { Bytes b = kdfw; b[7] = 1; mut("reserved byte 7 = 1", b, Status::ReservedByte); }
    { Bytes b = kdfw; b[6] = 0x02; mut("flags 0x02 (bit 1)", b, Status::UnknownFlags); }
    std::printf("  ->  A3 %d/12 refused with their reasons (predicted 12/12: 7 frozen + 5 in the review addendum)\n", a3);

    // ---- A4 / A5: the round-trip corpus and its sabotaged twins --------------------------------
    Bytes good, sab;
    if (!ReadFile("corpus-60mi.bin", &good) || !ReadFile("corpus-60mi-sabotaged.bin", &sab)) {
        std::printf("FAIL: cannot read the corpus (rig)\n");
        return 2;
    }
    auto records = [](const Bytes& all) {
        std::vector<Bytes> out;
        for (size_t i = 0; i + 4 <= all.size();) {
            const uint32_t n = all[i] | (all[i + 1] << 8) | (all[i + 2] << 16) | ((uint32_t)all[i + 3] << 24);
            out.push_back(Bytes(all.begin() + i + 4, all.begin() + i + 4 + n));
            i += 4 + n;
        }
        return out;
    };
    const std::vector<Bytes> gr = records(good), sr = records(sab);
    size_t at = j.find("\"records\": [");
    int a4 = 0, a5 = 0, twins = 0;
    for (size_t k = 0; k < gr.size() && k < sr.size(); ++k) {
        size_t p;
        const std::string want = StringAfter(j, at, "cellsSha256", &p);
        const size_t td = j.find("\"twinDiffers\": ", p);
        const bool differs = j.compare(td + 15, 4, "true") == 0;
        at = td + 1;
        Bytes codes;
        a4 += Decode(gr[k], &codes) == Status::Ok && sha::Hex(codes.data(), codes.size()) == want;
        if (differs) { ++twins; a5 += Refusal(sr[k]) != Status::Ok; }
    }
    check(gr.size() == 50 && a4 == 50, "A4", std::to_string(a4) + "/" + std::to_string(gr.size()) + " corpus crops round-trip to the encoder's cells");
    check(twins == 50 && a5 == 50, "A5", std::to_string(a5) + "/" + std::to_string(twins) + " sabotaged twins refused");
    std::printf("  ->  A4 %d/50 (predicted 50/50); A5 %d/%d (predicted 50/50)\n", a4, a5, twins);

    // ---- A7 / A8: KDFW's runs and header; KMSP's header ----------------------------------------
    long calls = 0;
    uint32_t p = 0;
    bool contiguous = true;
    wxcrop::Header h;
    g_counting = true;
    const Status s7 = wxcrop::ForEachRun(kdfw.data(), kdfw.size(), [&](uint16_t row, uint16_t col0, uint32_t len, uint8_t) {
        if (row != p / 208 || col0 != p % 208 || len == 0) contiguous = false;
        p += len;
        ++calls;
    }, &h);
    g_counting = false;
    check(s7 == Status::Ok && calls == 3741 && contiguous && p == 36192, "A7",
          std::to_string(calls) + " runs, " + (contiguous ? "row-major and contiguous" : "NOT contiguous") + ", " + std::to_string(p) + " cells (predicted 3,741 / 36,192)");
    check(h.rows == 174 && h.cols == 208 && h.coarsen == 1 && h.flags == 0 && h.frameEpoch == 1716328842u, "A8",
          "KDFW header: " + std::to_string(h.rows) + "x" + std::to_string(h.cols) + " coarsen " + std::to_string(h.coarsen) +
          " flags " + std::to_string(h.flags) + " frame " + std::to_string(h.frameEpoch));
    Bytes kmsp;
    ReadFile("kmsp-222km-coarsened.wxc", &kmsp);
    wxcrop::Header hm;
    const Status sm = wxcrop::ParseHeader(kmsp.data(), kmsp.size(), &hm);
    check(sm == Status::Ok && hm.coarsen == 2 && hm.flags == 1 && hm.Coarsened(), "A8",
          "KMSP header: coarsen " + std::to_string(hm.coarsen) + " flags " + std::to_string(hm.flags));

    // ---- A6: allocations across every decode above ---------------------------------------------
    check(g_allocs == 0, "A6", std::to_string(g_allocs) + " heap allocations during decoding (predicted 0)");

    // ---- extra, not in the frozen table ------------------------------------------------------
    { Bytes b(kdfw.begin(), kdfw.begin() + 31); check(Refusal(b) == Status::TooShort, "+", "31 bytes: TooShort (extra)"); }
    {
        Bytes small(100);
        const Status s = wxcrop::DecodeInto(kdfw.data(), kdfw.size(), small.data(), small.size());
        bool untouched = true;
        for (uint8_t v : small) untouched &= v == 0;
        check(s == Status::BufferTooSmall && untouched, "+", "a 100-cell buffer: BufferTooSmall, buffer untouched (extra)");
    }

    std::printf("%s\n", failures ? "WX CROP TESTS FAILED" : "all WX crop checks passed");
    return failures ? 1 : 0;
}
