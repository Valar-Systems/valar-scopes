// The model a device names itself by (include/OtaIdentity.h, ota::ModelString), against the rows frozen in
// docs/weatherscope-stage4.md S4.5: M1 over every row of test/fixtures/ota/identities.txt, the same table CI's
// ota-identity job holds equal to the fleet (so the count follows the fleet and can't go stale), and M1b, a short
// buffer refused rather than cut. Today: 9 rows, 6 without a scope, so 9 + 6 + 3 = 18 checks.
//
//     test_ota_model <test/fixtures/ota>
//
// Exit 0 every check holds; 1 a check failed; 2 the rig (the table can't be read, or holds no row).

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "OtaIdentity.h"

namespace {

int checks = 0, failures = 0;

void check(bool ok, const std::string& id, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id.c_str(), what.c_str());
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_ota_model <test/fixtures/ota>\n");
        return 2;
    }
    std::ifstream f((std::string(argv[1]) + "/identities.txt").c_str());
    if (!f) {
        std::printf("RIG: cannot read %s/identities.txt\n", argv[1]);
        return 2;
    }
    int rows = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string env, scope, board;
        if (!(in >> env >> scope >> board)) continue;
        ++rows;
        const std::string s = scope == "-" ? "" : scope;
        const ota::Identity id = ota::Request(board.c_str(), s.c_str());
        char out[2 * ota::kFieldMax + 1];
        const bool made = ota::ModelString(id, out, sizeof out);
        const std::string got = made ? out : "(refused)";
        check(made && got == s + board, "M1-" + env, "model '" + got + "', want '" + s + board + "'");
        if (s.empty())
            check(made && got == board, "M1r-" + env, "the radar's model is its board, '" + board + "', as variant::SLUG is");
    }
    if (rows == 0) {
        std::printf("RIG: no rows in %s/identities.txt\n", argv[1]);
        return 2;
    }

    // M1b: a model that does not fit is refused and nothing is written; an exact fit is accepted. M1b grades refusal
    // against acceptance only: what the model SAYS is M1's, so a plant of the text (S-M1) leaves M1b green (S4.5).
    const ota::Identity wx = ota::Request("s3-128", "weatherscope-");
    const size_t need = std::strlen("weatherscope-s3-128") + 1;
    char buf[64];
    check(ota::ModelString(wx, buf, need), "M1b-fit", "an exact fit is accepted, not refused");
    std::memset(buf, 'Z', sizeof buf);
    const bool shortOk = ota::ModelString(wx, buf, need - 1);
    bool untouched = true;
    for (size_t i = 0; i < sizeof buf; ++i) untouched &= buf[i] == 'Z';
    check(!shortOk && untouched, "M1b-short", "one byte short: refused, and nothing written");
    const ota::Identity none = ota::Request("this-board-name-is-far-too-long-for-a-field", "");
    std::memset(buf, 'Z', sizeof buf);
    const bool noneOk = ota::ModelString(none, buf, sizeof buf);
    untouched = true;
    for (size_t i = 0; i < sizeof buf; ++i) untouched &= buf[i] == 'Z';
    check(!noneOk && untouched, "M1b-noboard", "an identity with no board (a refused request) has no model");

    std::printf("%d rows; %d checks, %d failures\n", rows, checks, failures);
    return failures ? 1 : 0;
}
