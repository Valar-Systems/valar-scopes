// Board addendum 2's device anchor, the file mode's verdict (docs/speed-p5p6.md section 7d, frozen): the seal's printed
// lines and the two grabs in, PASS / FAIL / VOID out. Pure C++11; anchor_compare.cpp reads the files and calls Judge.
//
//   exit 0 PASS   every precondition holds and the composite matches over the region (mismatch 0)
//   exit 1 FAIL   the preconditions hold and the mismatch is > 0
//   exit 2 VOID   a refusal, with its reason: a malformed anchor line or a grab of the wrong length; a print missing,
//                 doubled, or not matching its count; A-STILL broken; S_bg carrying a contact or S_8copy none; a
//                 nonzero ovl, a trk not a multiple of 10, an up not 0; no overlapping pair; no edge-clipped contact
#pragma once
#include <cstdio>
#include <cstdlib>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "anchor_core.h"

namespace anchorfile {

constexpr int kCap = 64;   // FrameBench.h's kAnchorCap: a print that counts more than it lists is refused

struct Line {
    int c, x, y, trk, cat, dot, up;
    unsigned col, ovl;
};
struct Print {
    int count;
    int summaries;
    std::vector<Line> k;
};

struct Verdict {
    int code;
    std::string reason;
    long region, mismatch;
    int contacts, pairs, clipped;
};

inline Verdict Make(int code, const std::string& why)
{
    Verdict v;
    v.code = code;
    v.reason = why;
    v.region = v.mismatch = 0;
    v.contacts = v.pairs = v.clipped = 0;
    return v;
}

// Every "[speed-p5] anchor " line of text, keyed by (scene, pass). False, with why, on any line that is not exactly
// one of the two frozen shapes. Everything else in the text (other tags, timestamps) is ignored.
inline bool Parse(const std::string& text, std::map<std::pair<int, int>, Print>& out, std::string& why)
{
    static const std::regex sum("\\[speed-p5\\] anchor S(\\d+) p([01]) contacts (\\d+)");
    static const std::regex con("\\[speed-p5\\] anchor S(\\d+) p([01]) c(\\d+) x (-?\\d+) y (-?\\d+) trk (-?\\d+) "
                                "cat (\\d+) dot ([01]) up (-?\\d+) col ([0-9a-f]{4}) ovl ([0-9a-f]{3})");
    std::istringstream in(text);
    std::string ln;
    while (std::getline(in, ln)) {
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        const size_t at = ln.find("[speed-p5] anchor ");
        if (at == std::string::npos) continue;
        const std::string body = ln.substr(at);
        std::smatch m;
        if (std::regex_match(body, m, sum)) {
            Print& p = out[std::make_pair(std::atoi(m[1].str().c_str()), std::atoi(m[2].str().c_str()))];
            p.summaries++;
            p.count = std::atoi(m[3].str().c_str());
        } else if (std::regex_match(body, m, con)) {
            Print& p = out[std::make_pair(std::atoi(m[1].str().c_str()), std::atoi(m[2].str().c_str()))];
            Line l;
            l.c = std::atoi(m[3].str().c_str());
            l.x = std::atoi(m[4].str().c_str());
            l.y = std::atoi(m[5].str().c_str());
            l.trk = std::atoi(m[6].str().c_str());
            l.cat = std::atoi(m[7].str().c_str());
            l.dot = std::atoi(m[8].str().c_str());
            l.up = std::atoi(m[9].str().c_str());
            l.col = (unsigned)std::strtoul(m[10].str().c_str(), nullptr, 16);
            l.ovl = (unsigned)std::strtoul(m[11].str().c_str(), nullptr, 16);
            p.k.push_back(l);
        } else {
            why = "malformed anchor line: " + body.substr(0, 100);
            return false;
        }
    }
    return true;
}

inline bool SameLine(const Line& a, const Line& b)
{
    return a.c == b.c && a.x == b.x && a.y == b.y && a.trk == b.trk && a.cat == b.cat && a.dot == b.dot &&
           a.up == b.up && a.col == b.col && a.ovl == b.ovl;
}

// One scene's two prints, checked whole; false with why. The summary is filled with the p0 print.
inline bool ScenePrints(const std::map<std::pair<int, int>, Print>& all, int s, Print& p0out, std::string& why)
{
    const Print* p[2] = {nullptr, nullptr};
    for (int pass = 0; pass < 2; ++pass) {
        const auto it = all.find(std::make_pair(s, pass));
        if (it == all.end()) {
            why = "missing print: S" + std::to_string(s) + " p" + std::to_string(pass);
            return false;
        }
        const Print& q = it->second;
        if (q.summaries != 1) {
            why = "S" + std::to_string(s) + " p" + std::to_string(pass) + " has " + std::to_string(q.summaries) +
                  " summary lines, not 1 (count)";
            return false;
        }
        if (q.count > kCap || (int)q.k.size() != q.count) {
            why = "S" + std::to_string(s) + " p" + std::to_string(pass) + " counts " + std::to_string(q.count) +
                  " and lists " + std::to_string(q.k.size()) + " (count)";
            return false;
        }
        for (int i = 0; i < (int)q.k.size(); ++i)
            if (q.k[i].c != i) {
                why = "S" + std::to_string(s) + " p" + std::to_string(pass) + " lists its contacts out of order";
                return false;
            }
        p[pass] = &q;
    }
    bool still = p[0]->count == p[1]->count;
    for (int i = 0; still && i < p[0]->count; ++i) still = SameLine(p[0]->k[i], p[1]->k[i]);
    if (!still) {
        why = "A-STILL: S" + std::to_string(s) + "'s p0 and p1 differ";
        return false;
    }
    p0out = *p[0];
    return true;
}

// The verdict, whole.
inline Verdict Judge(const std::vector<uint8_t>& bgBody, const std::vector<uint8_t>& devBody, const std::string& text,
                     int sBg, int s8)
{
    std::unique_ptr<anchor::Img> bg = anchor::NewImg(), dev = anchor::NewImg();
    if (!anchor::Decode(bgBody, *bg)) return Make(2, "the BG grab is " + std::to_string(bgBody.size()) + " bytes");
    if (!anchor::Decode(devBody, *dev)) return Make(2, "the 8COPY grab is " + std::to_string(devBody.size()) + " bytes");
    std::map<std::pair<int, int>, Print> all;
    std::string why;
    if (!Parse(text, all, why)) return Make(2, why);
    Print pb, p8;
    if (!ScenePrints(all, sBg, pb, why)) return Make(2, why);
    if (!ScenePrints(all, s8, p8, why)) return Make(2, why);
    if (pb.count != 0) return Make(2, "BG: S" + std::to_string(sBg) + " prints " + std::to_string(pb.count) + " contacts");
    if (p8.count == 0) return Make(2, "8COPY empty: S" + std::to_string(s8) + " prints no contact");
    anchor::Frame f;
    f.up = 0;
    f.zoomed = false;
    for (const Line& l : p8.k) {
        if (l.ovl != 0) return Make(2, "ovl: c" + std::to_string(l.c) + " carries an overlay");
        if (l.trk % 10 != 0) return Make(2, "trk: c" + std::to_string(l.c) + " is not whole degrees");
        if (l.up != 0) return Make(2, "up: c" + std::to_string(l.c) + " is not north-up");
        f.k.push_back(anchor::Contact{l.cat, (float)(l.trk / 10), l.x, l.y, l.dot != 0, (uint32_t)l.col});
    }
    Verdict v = Make(0, "");
    v.contacts = (int)f.k.size();
    v.pairs = anchor::OverlapPairs(f);
    for (size_t i = 0; i < f.k.size(); ++i) v.clipped += anchor::EdgeClipped(f, i);
    if (v.pairs < 1) { v.code = 2; v.reason = "no overlapping pair"; return v; }
    if (v.clipped < 1) { v.code = 2; v.reason = "no edge-clipped contact"; return v; }
    const anchor::Result r = anchor::Anchor(*bg, *dev, f);
    v.region = r.region;
    v.mismatch = r.mismatch;
    v.code = r.mismatch == 0 ? 0 : 1;
    v.reason = r.mismatch == 0 ? "PASS" : "FAIL";
    return v;
}

// ---- section 10's marker A/B (docs/speed-p5p6.md section 10, accepted 2026-11-17) ----
//
// ONE scene grabbed in two images, M (main's marker arithmetic) and P (P5'), with each image's own anchor lines for
// it. The region is the union of the 8-copy footprints dilated 2 (anchor::Region): a superset of every marker pixel.
// Pixels inside the clock and indicator boxes are never judged.
//   code 0  no pixel differs outside the boxes
//   code 1  differences, every one inside the region
//   code 2  a difference outside the region (something other than a marker differs: STOP)
//   code 3  VOID: a grab of the wrong length; a malformed anchor line; a print missing, doubled, or not matching its
//           count; A-STILL broken in either image; A-SAME broken (M's lines and P's differ in count or in any of
//           x y trk cat dot up col ovl); the scene prints no contact; a nonzero ovl; an up not 0
// Section 10 froze 0, 1 and 2 and named the VOID conditions without a code; 3 keeps VOID apart from exit 2's STOP.

struct AbVerdict {
    int code;
    std::string reason;
    long region, in, out, boxed;
    int contacts;
};

inline AbVerdict AbMake(int code, const std::string& why)
{
    AbVerdict v;
    v.code = code;
    v.reason = why;
    v.region = v.in = v.out = v.boxed = 0;
    v.contacts = 0;
    return v;
}

inline AbVerdict JudgeAB(const std::vector<uint8_t>& mBody, const std::vector<uint8_t>& pBody, const std::string& mText,
                         const std::string& pText, int s)
{
    std::unique_ptr<anchor::Img> m = anchor::NewImg(), p = anchor::NewImg();
    if (!anchor::Decode(mBody, *m)) return AbMake(3, "M's grab is " + std::to_string(mBody.size()) + " bytes");
    if (!anchor::Decode(pBody, *p)) return AbMake(3, "P's grab is " + std::to_string(pBody.size()) + " bytes");
    std::map<std::pair<int, int>, Print> am, ap;
    std::string why;
    if (!Parse(mText, am, why)) return AbMake(3, "M: " + why);
    if (!Parse(pText, ap, why)) return AbMake(3, "P: " + why);
    Print pm, pp;
    if (!ScenePrints(am, s, pm, why)) return AbMake(3, "M: " + why);   // A-STILL in M, among the print checks
    if (!ScenePrints(ap, s, pp, why)) return AbMake(3, "P: " + why);   // and in P
    if (pm.count == 0 && pp.count == 0) return AbMake(3, "empty: S" + std::to_string(s) + " prints no contact");
    bool same = pm.count == pp.count;
    for (int i = 0; same && i < pm.count; ++i) same = SameLine(pm.k[i], pp.k[i]);
    if (!same)
        return AbMake(3, "A-SAME: M's and P's S" + std::to_string(s) + " lines differ (" + std::to_string(pm.count) +
                             " and " + std::to_string(pp.count) + " contacts)");
    anchor::Frame f;
    f.up = 0;
    f.zoomed = false;
    for (const Line& l : pm.k) {
        if (l.ovl != 0) return AbMake(3, "ovl: c" + std::to_string(l.c) + " carries an overlay");
        if (l.up != 0) return AbMake(3, "up: c" + std::to_string(l.c) + " is not north-up");
        f.k.push_back(anchor::Contact{l.cat, (float)l.trk / 10.0f, l.x, l.y, l.dot != 0, (uint32_t)l.col});
    }
    const std::vector<uint8_t> r = anchor::Region(f);   // footprints dilated 2, the boxes already removed
    AbVerdict v = AbMake(0, "");
    v.contacts = (int)f.k.size();
    for (int i = 0; i < anchor::W * anchor::W; ++i) {
        v.region += r[i];
        if (m->p[i] == p->p[i]) continue;
        if (anchor::Excluded(i % anchor::W, i / anchor::W)) {
            ++v.boxed;
            continue;
        }
        if (r[i])
            ++v.in;
        else
            ++v.out;
    }
    v.code = v.out ? 2 : v.in ? 1 : 0;
    v.reason = v.out ? "a difference OUTSIDE every marker region: STOP"
                     : v.in ? "differences only inside the marker region" : "no px differs outside the boxes";
    return v;
}

}  // namespace anchorfile
