// The OTA identity record and the accept rule (include/OtaIdentity.h), frozen in
// docs/weatherscope-stage3.md A12.103 (Q75 (b)) and A12.104 before this code.
//
//   test_ota_identity <test/fixtures/ota>
//
// P1-P3   the real heads parse to their board and scope
// P4-P12  one check per parse rule
// R1      a request always names the device's own board: 7 envs x 3 scopes
// M1      O1-b (i), the switch matrix: asking for the candidate's scope on one's own board
//         accepts exactly the same-board pairs -- 27 accepted, 22 refused
// M2      no scope at all makes a cross-board image acceptable: 66 of 66 refused
// M3      O1-b (ii), asking for one's own scope: 11 accepted, 38 refused, 16 of them cross-scope
// (over the 7 built envs with an OTA path: the s3-146 rows are retired, A12.105, A12.107)
// O4-*    the same rule on the real heads: own (CONTROL), a same-board other-scope image both
//         ways, the switch, cross-board both ways
//
// The candidates in M1-M3 are not built with Request: each is encoded as a record by this
// test's own writer (A12.104's byte table) and parsed back, so the parser and the layout are
// checked against an independent writer on every pair.
//
// The counts are today's skus.yml (test/fixtures/ota/identities.txt, which CI holds equal to
// what the fleet derives). A new env changes them, and that is deliberate: a new identity is
// the moment to re-read the rule, so the frozen numbers fail rather than follow.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "OtaIdentity.h"

namespace {

int checks = 0, failures = 0;

void check(bool ok, const char* id, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

std::string root;

bool Load(const std::string& rel, std::vector<uint8_t>& out)
{
    std::ifstream f((root + "/" + rel).c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

struct Env {
    std::string env, scope, board;
};

// A12.104's byte table, written independently of Make() in src/OtaIdentity.cpp.
std::vector<uint8_t> EncodeHead(const std::string& board, const std::string& scope)
{
    std::vector<uint8_t> h(ota::kHeadBytes, 0);
    h[0] = 0xE9;
    uint8_t* r = &h[ota::kRecordOffset];
    r[0] = 0x56; r[1] = 0x53; r[2] = 0x49; r[3] = 0x44;   // "VSID"
    r[4] = 1;
    r[5] = 64;
    r[6] = (uint8_t)board.size();
    r[7] = (uint8_t)scope.size();
    std::memcpy(r + 8, board.data(), board.size());
    std::memcpy(r + 32, scope.data(), scope.size());
    return h;
}

ota::Identity FromHead(const std::vector<uint8_t>& head, ota::Parsed* how = nullptr)
{
    ota::Identity id;
    std::memset(&id, 0, sizeof id);
    const ota::Parsed p = ota::Parse(head.data(), head.size(), id);
    if (how) *how = p;
    return id;
}

const char* Name(ota::Parsed p)
{
    return p == ota::Parsed::Ok ? "ok" : p == ota::Parsed::NoRecord ? "no-record" : "bad-record";
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_ota_identity <test/fixtures/ota>\n");
        return 2;
    }
    root = argv[1];

    std::vector<uint8_t> radar, wx, s21;
    if (!Load("heads/blipscope-s3-128.head.bin", radar) || !Load("heads/weatherscope-s3-128.head.bin", wx) ||
        !Load("heads/blipscope-pro-s3-21.head.bin", s21) || radar.size() != ota::kHeadBytes ||
        wx.size() != ota::kHeadBytes || s21.size() != ota::kHeadBytes) {
        std::printf("RIG: the heads in %s/heads are missing or not %u bytes\n", root.c_str(), (unsigned)ota::kHeadBytes);
        return 2;
    }
    std::vector<Env> envs;
    {
        std::ifstream f((root + "/identities.txt").c_str());
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            Env e;
            ss >> e.env >> e.scope >> e.board;
            if (e.scope == "-") e.scope.clear();
            envs.push_back(e);
        }
    }
    if (envs.empty()) {
        std::printf("RIG: no rows in %s/identities.txt\n", root.c_str());
        return 2;
    }

    // ---- P: parsing
    {
        struct Want { const char* id; const std::vector<uint8_t>* head; const char* board; const char* scope; };
        const Want wants[] = {{"P1", &radar, "s3-128", ""}, {"P2", &wx, "s3-128", "weatherscope-"},
                              {"P3", &s21, "s3-21", ""}};
        for (const Want& w : wants) {
            ota::Parsed p;
            const ota::Identity id = FromHead(*w.head, &p);
            check(p == ota::Parsed::Ok && std::strcmp(id.board, w.board) == 0 && std::strcmp(id.scope, w.scope) == 0,
                  w.id, std::string("real head -> ") + Name(p) + " board=" + id.board + " scope=" + id.scope);
        }
    }
    {
        struct Rule { const char* id; const char* what; int at; int value; ota::Parsed want; };
        const int R = (int)ota::kRecordOffset;
        const Rule rules[] = {
            {"P4", "a wrong magic", R + 0, 0x57, ota::Parsed::NoRecord},
            {"P5", "version 2", R + 4, 2, ota::Parsed::BadRecord},
            {"P6", "length 63", R + 5, 63, ota::Parsed::BadRecord},
            {"P7", "a board length of 0", R + 6, 0, ota::Parsed::BadRecord},
            {"P8", "a board length of 25", R + 6, 25, ota::Parsed::BadRecord},
            {"P9", "a scope length of 25", R + 7, 25, ota::Parsed::BadRecord},
            {"P10", "a byte outside [a-z0-9-]", R + 8, 'S', ota::Parsed::BadRecord},
            {"P11", "a non-zero byte after the board's length", R + 8 + 6, 'x', ota::Parsed::BadRecord},
            {"P12", "a non-zero reserved byte", R + 63, 1, ota::Parsed::BadRecord},
        };
        for (const Rule& rule : rules) {
            std::vector<uint8_t> h = radar;
            h[rule.at] = (uint8_t)rule.value;
            ota::Parsed p;
            FromHead(h, &p);
            check(p == rule.want, rule.id, std::string(rule.what) + " -> " + Name(p) + " (want " + Name(rule.want) + ")");
        }
    }

    // ---- R1 + the matrices
    std::set<std::string> scopeSet;
    for (const Env& e : envs) scopeSet.insert(e.scope);
    const std::vector<std::string> scopes(scopeSet.begin(), scopeSet.end());
    std::vector<ota::Identity> cand;
    for (const Env& e : envs) cand.push_back(FromHead(EncodeHead(e.board, e.scope)));

    {
        int good = 0, n = 0;
        for (const Env& r : envs)
            for (const std::string& s : scopes) {
                const ota::Identity q = ota::Request(r.board.c_str(), s.c_str());
                ++n;
                good += r.board == q.board && s == q.scope;
            }
        char buf[96];
        std::snprintf(buf, sizeof buf, "a request names the asker's own board: %d/%d (%u envs x %u scopes)", good, n,
                      (unsigned)envs.size(), (unsigned)scopes.size());
        check(good == n && n == 21, "R1", buf);
    }
    {
        int acc = 0, ref = 0, off = 0;
        for (size_t i = 0; i < envs.size(); ++i)
            for (size_t j = 0; j < envs.size(); ++j) {
                const bool a = ota::Accept(ota::Request(envs[i].board.c_str(), envs[j].scope.c_str()), cand[j]);
                (a ? acc : ref) += 1;
                off += a != (envs[i].board == envs[j].board);
            }
        char buf[128];
        std::snprintf(buf, sizeof buf, "switch matrix: %d accepted, %d refused, %d off the rule (want 27, 22, 0)", acc,
                      ref, off);
        check(acc == 27 && ref == 22 && off == 0, "M1", buf);
    }
    {
        int ref = 0, n = 0;
        for (size_t i = 0; i < envs.size(); ++i)
            for (size_t j = 0; j < envs.size(); ++j) {
                if (envs[i].board == envs[j].board) continue;
                for (const std::string& s : scopes) {
                    ++n;
                    ref += !ota::Accept(ota::Request(envs[i].board.c_str(), s.c_str()), cand[j]);
                }
            }
        char buf[128];
        std::snprintf(buf, sizeof buf, "cross-board, every scope asked for: %d of %d refused (want 66 of 66)", ref, n);
        check(ref == n && n == 66, "M2", buf);
    }
    {
        int acc = 0, ref = 0, crossScope = 0, off = 0;
        for (size_t i = 0; i < envs.size(); ++i)
            for (size_t j = 0; j < envs.size(); ++j) {
                const bool a = ota::Accept(ota::Request(envs[i].board.c_str(), envs[i].scope.c_str()), cand[j]);
                const bool same = envs[i].board == envs[j].board && envs[i].scope == envs[j].scope;
                (a ? acc : ref) += 1;
                off += a != same;
                crossScope += !a && envs[i].board == envs[j].board && envs[i].scope != envs[j].scope;
            }
        char buf[160];
        std::snprintf(buf, sizeof buf,
                      "own-scope matrix: %d accepted, %d refused (%d cross-scope), %d off the rule (want 11, 38 (16), 0)",
                      acc, ref, crossScope, off);
        check(acc == 11 && ref == 38 && crossScope == 16 && off == 0, "M3", buf);
    }

    // ---- O4 on the real heads
    {
        const ota::Identity radarId = FromHead(radar), wxId = FromHead(wx), s21Id = FromHead(s21);
        const ota::Identity own128 = ota::Request("s3-128", ""), wx128 = ota::Request("s3-128", "weatherscope-");
        const ota::Identity own21 = ota::Request("s3-21", "");
        check(ota::Accept(own128, radarId), "O4-own", "s3-128 asking for its own scope accepts the real s3-128 head (CONTROL)");
        check(!ota::Accept(own128, wxId), "O4-ii", "s3-128 asking for its own scope refuses the real Weatherscope head");
        check(!ota::Accept(wx128, radarId), "O4-iii", "asking for weatherscope-s3-128 refuses the real s3-128 head");
        check(ota::Accept(wx128, wxId), "O4-iv", "the switch: an s3-128 asking for weatherscope- accepts the real Weatherscope head");
        check(!ota::Accept(own128, s21Id), "O4-v-a", "an s3-128 refuses the real blipscope-pro-s3-21 head");
        check(!ota::Accept(own21, radarId), "O4-v-b", "an s3-21 refuses the real s3-128 head");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
