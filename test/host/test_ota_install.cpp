// The install decision (include/OtaInstall.h): a refused image never touches flash.
// Frozen in docs/weatherscope-stage3.md A1.3 (O7, O8, O10), A12.103 and A12.104 before this code.
//
//   test_ota_install <test/fixtures/ota>
//
// The fake flash logs every call in order and keeps a boot HISTORY; its end() switches
// the boot to the target slot, as Update.end() does, and revert() exists only for the
// withdrawn design's sabotage (S-O8a). The history is the assertion, not the final
// state: the withdrawn design switches back, so a final-state check would pass it.
// Every refused input prints its final boot label as an info line to show exactly that.
//
// I1-I6  six refused inputs, four checks each (-why, -log empty, -boot initial only,
//        -target unchanged): cross-board (foreign), same board other scope (foreign),
//        v18's published pre-check head (no-record), a wrong magic (no-record), version 2
//        (bad-record), a stream cut at 200 bytes (truncated)
// C1, C2 CONTROLS, four checks each: the device's own image, and the switch (asking for
//        weatherscope- on an s3-128): installed, log begin/write/writeRest/end, the
//        history switches once, the target equals the image. Without them a fake that
//        ignored every call would pass I1-I6.
// O10    the 8 identities each asking for their own scope, against the two pre-check heads (v18's
//        published radar image, and the base Weatherscope build): 16 no-record, 0 foreign, 0 flash calls
//        (8 since the wxlook bench env joined, 2026-11-06, #445)
//        (the s3-146 rows are retired, A12.107; no CI-built Missileer image exists, A12.110)
// MAP1-3 the fail mapping (A12.104): the refusal tokens; a short non-image is fail--106 with
//        no flash call (not truncated); a begin failure is fail-<its error>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "OtaInstall.h"

namespace {

int checks = 0, failures = 0;

void check(bool ok, const std::string& id, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

std::string root;

bool Load(const std::string& rel, std::vector<uint8_t>& out)
{
    std::ifstream f((root + "/" + rel).c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

constexpr size_t kImage = 64 * 1024;    // every whole image: the head + a patterned body
constexpr size_t kSlot = 128 * 1024;    // the target slot
constexpr size_t kChunk = 100;          // the source never hands over more than this per read

std::vector<uint8_t> Image(const std::vector<uint8_t>& head)
{
    std::vector<uint8_t> img(head);
    while (img.size() < kImage) img.push_back((uint8_t)(img.size() * 7 + 3));
    return img;
}

struct FakeSource {
    const std::vector<uint8_t>& data;
    size_t pos, cut;
    FakeSource(const std::vector<uint8_t>& d, size_t c) : data(d), pos(0), cut(c < d.size() ? c : d.size()) {}
    size_t read(uint8_t* buf, size_t n)
    {
        size_t k = cut - pos;
        if (k > n) k = n;
        if (k > kChunk) k = kChunk;
        std::memcpy(buf, data.data() + pos, k);
        pos += k;
        return k;
    }
};

struct FakeFlash {
    std::vector<std::string> log;
    std::vector<std::string> boot;
    std::vector<uint8_t> target;
    size_t pos;
    int err;
    int failBegin;   // 0, or the error begin() fails with
    FakeFlash() : boot(1, "app0"), target(kSlot, 0xFF), pos(0), err(0), failBegin(0) {}

    bool begin(size_t total)
    {
        log.push_back("begin");
        if (failBegin) { err = failBegin; return false; }
        if (total > target.size()) { err = 4; return false; }
        pos = 0;
        return true;
    }
    size_t write(const uint8_t* buf, size_t n)
    {
        log.push_back("write");
        std::memcpy(&target[pos], buf, n);
        pos += n;
        return n;
    }
    template <class Source>
    size_t writeRest(Source& src, size_t n)
    {
        log.push_back("writeRest");
        uint8_t buf[512];
        size_t done = 0;
        while (done < n) {
            const size_t got = src.read(buf, n - done < sizeof buf ? n - done : sizeof buf);
            if (got == 0) { err = 6; return done; }
            std::memcpy(&target[pos], buf, got);
            pos += got;
            done += got;
        }
        return done;
    }
    bool end()
    {
        log.push_back("end");
        boot.push_back("app1");
        return true;
    }
    void revert()   // the withdrawn design's flip-back; only S-O8a calls it
    {
        log.push_back("revert");
        boot.push_back("app0");
    }
    int error() const { return err; }
};

std::string Join(const std::vector<std::string>& v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s.empty() ? "-" : s;
}

const char* OutcomeName(ota::Outcome o)
{
    switch (o) {
        case ota::Outcome::Accepted: return "accepted";
        case ota::Outcome::Installed: return "installed";
        case ota::Outcome::NotAnImage: return "not-an-image";
        case ota::Outcome::Truncated: return "truncated";
        case ota::Outcome::NoRecord: return "no-record";
        case ota::Outcome::BadRecord: return "bad-record";
        case ota::Outcome::Foreign: return "foreign";
        case ota::Outcome::FlashFailed: return "flash-failed";
    }
    return "?";
}

bool Untouched(const FakeFlash& f)
{
    for (size_t i = 0; i < f.target.size(); ++i)
        if (f.target[i] != 0xFF) return false;
    return true;
}

void Refused(const char* id, const char* what, const std::vector<uint8_t>& img, size_t cut, const ota::Identity& req,
             ota::Outcome want)
{
    FakeFlash flash;
    FakeSource src(img, cut);
    const ota::Result r = ota::Install(src, flash, img.size(), req);
    const std::string I(id);
    check(r.outcome == want, I + "-why", std::string(what) + ": " + OutcomeName(r.outcome) + " (want " + OutcomeName(want) + ")");
    check(flash.log.empty(), I + "-log", "flash calls: " + Join(flash.log) + " (want none)");
    check(flash.boot.size() == 1 && flash.boot[0] == "app0", I + "-boot", "boot history: " + Join(flash.boot) + " (want app0 only)");
    check(Untouched(flash), I + "-target", Untouched(flash) ? "target slot unchanged" : "target slot WRITTEN");
    std::printf("  info  %s final boot=%s (a final-state check would read this alone)\n", id, flash.boot.back().c_str());
}

void Installs(const char* id, const char* what, const std::vector<uint8_t>& img, const ota::Identity& req)
{
    FakeFlash flash;
    FakeSource src(img, img.size());
    const ota::Result r = ota::Install(src, flash, img.size(), req);
    const std::string I(id);
    check(r.outcome == ota::Outcome::Installed, I + "-why", std::string(what) + ": " + OutcomeName(r.outcome) + " (want installed)");
    check(Join(flash.log) == "begin,write,writeRest,end", I + "-log", "flash calls: " + Join(flash.log) + " (want begin,write,writeRest,end)");
    check(Join(flash.boot) == "app0,app1", I + "-boot", "boot history: " + Join(flash.boot) + " (want app0,app1)");
    check(std::memcmp(flash.target.data(), img.data(), img.size()) == 0, I + "-target", "target equals the image");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_ota_install <test/fixtures/ota>\n");
        return 2;
    }
    root = argv[1];
    const char* names[] = {"blipscope-s3-128", "weatherscope-s3-128", "blipscope-pro-s3-21",
                           "pre-blipscope-s3-128", "pre-weatherscope-s3-128"};
    const int kHeads = (int)(sizeof names / sizeof names[0]), kFirstPre = 3;
    std::vector<std::vector<uint8_t> > heads(kHeads);
    for (int i = 0; i < kHeads; ++i)
        if (!Load(std::string("heads/") + names[i] + ".head.bin", heads[i]) || heads[i].size() != ota::kHeadBytes) {
            std::printf("RIG: heads/%s.head.bin is missing or not %u bytes\n", names[i], (unsigned)ota::kHeadBytes);
            return 2;
        }
    const std::vector<uint8_t>&radar = heads[0], &wx = heads[1], &s21 = heads[2];

    const ota::Identity own = ota::Request("s3-128", "");
    const ota::Identity toWx = ota::Request("s3-128", "weatherscope-");

    // ---- I1-I6: refused, and no flash call of any kind
    {
        std::vector<uint8_t> badMagic = radar, v2 = radar;
        badMagic[ota::kRecordOffset] = 0x57;
        v2[ota::kRecordOffset + 4] = 2;
        Refused("I1", "the blipscope-pro-s3-21 head, asked for as s3-128", Image(s21), kImage, own, ota::Outcome::Foreign);
        Refused("I2", "the Weatherscope head, asked for as s3-128", Image(wx), kImage, own, ota::Outcome::Foreign);
        Refused("I3", "v18's published pre-check s3-128 head", Image(heads[kFirstPre]), kImage, own,
                ota::Outcome::NoRecord);
        Refused("I4", "a wrong magic", Image(badMagic), kImage, own, ota::Outcome::NoRecord);
        Refused("I5", "record version 2", Image(v2), kImage, own, ota::Outcome::BadRecord);
        Refused("I6", "a stream cut at 200 bytes", Image(radar), 200, own, ota::Outcome::Truncated);
    }

    // ---- C1, C2: the controls
    Installs("C1", "the device's own image", Image(radar), own);
    Installs("C2", "the switch: weatherscope- asked for on an s3-128", Image(wx), toWx);

    // ---- O10: every identity refuses every pre-check head, before any flash call
    {
        std::ifstream f((root + "/identities.txt").c_str());
        std::string line;
        int noRecord = 0, foreign = 0, other = 0, calls = 0, rows = 0;
        while (std::getline(f, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string env, scope, board;
            ss >> env >> scope >> board;
            if (scope == "-") scope.clear();
            ++rows;
            for (int h = kFirstPre; h < kHeads; ++h) {
                const std::vector<uint8_t> img = Image(heads[h]);
                FakeFlash flash;
                FakeSource src(img, img.size());
                const ota::Result r = ota::Install(src, flash, img.size(), ota::Request(board.c_str(), scope.c_str()));
                calls += (int)flash.log.size();
                if (r.outcome == ota::Outcome::NoRecord) ++noRecord;
                else if (r.outcome == ota::Outcome::Foreign) ++foreign;
                else ++other;
            }
        }
        char buf[160];
        std::snprintf(buf, sizeof buf, "%d identities x %d pre-check heads: %d no-record, %d foreign, %d other, %d flash calls "
                      "(want 16, 0, 0, 0)", rows, kHeads - kFirstPre, noRecord, foreign, other, calls);
        check(rows == 8 && noRecord == 16 && foreign == 0 && other == 0 && calls == 0, "O10", buf);
    }

    // ---- MAP1-3: the fail mapping
    {
        ota::Result r;
        std::memset(&r, 0, sizeof r);
        const ota::Outcome os[] = {ota::Outcome::Truncated, ota::Outcome::NoRecord, ota::Outcome::BadRecord,
                                   ota::Outcome::Foreign};
        const char* want[] = {"refused-truncated", "refused-no-record", "refused-bad-record", "refused-foreign"};
        bool ok = true;
        std::string got;
        for (int i = 0; i < 4; ++i) {
            char t[32];
            r.outcome = os[i];
            ota::Token(r, t, sizeof t);
            ok &= std::strcmp(t, want[i]) == 0;
            got += std::string(i ? " " : "") + t;
        }
        check(ok, "MAP1", "refusal tokens: " + got);
    }
    {
        std::vector<uint8_t> page(100, ' ');
        const char* html = "<!DOCTYPE html><html><body>Not Found</body></html>";
        std::memcpy(page.data(), html, std::strlen(html));
        FakeFlash flash;
        FakeSource src(page, page.size());
        const ota::Result r = ota::Install(src, flash, page.size(), own);
        char t[32];
        ota::Token(r, t, sizeof t);
        check(std::strcmp(t, "fail--106") == 0 && flash.log.empty(), "MAP2",
              std::string("a 100-byte HTML page: ") + t + ", flash calls " + Join(flash.log) + " (want fail--106, none)");
    }
    {
        const std::vector<uint8_t> img = Image(radar);
        FakeFlash flash;
        flash.failBegin = 4;
        FakeSource src(img, img.size());
        const ota::Result r = ota::Install(src, flash, img.size(), own);
        char t[32];
        ota::Token(r, t, sizeof t);
        check(std::strcmp(t, "fail-4") == 0 && Join(flash.log) == "begin", "MAP3",
              std::string("begin fails with 4: ") + t + ", flash calls " + Join(flash.log) + " (want fail-4, begin)");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
