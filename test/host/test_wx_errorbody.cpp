// W7's refusal reason (docs/weatherscope-stage4.md S4.43): wxerror::ErrorOf reads the proxy's {"error":"<token>"} from a
// 401/403 body only when the body holds one whole. With no body -- what GetBytes gives a non-200 today -- a 403 maps to
// Other (ACCESS REFUSED), never to a guessed kind; a token too long for its buffer is refused, never cut. Pure C++11.
// Its sabotages: test/host/sabotage_wx_errorbody.py.
//
//   g++ -std=c++11 -Iinclude -Isrc/weatherscope test/host/test_wx_errorbody.cpp
//
// Prints ids and tokens only.
#include <cstdio>
#include <cstring>
#include <string>

#include "WxErrorBody.h"
#include "WxSetup.h"

namespace {

int failures = 0;

void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

std::string Read(const char* body, size_t cap = 24)
{
    char out[64];
    wxerror::ErrorOf((const uint8_t*)body, body ? std::strlen(body) : 0, out, cap);
    return out;
}

using wxsetup::Refusal;

}  // namespace

int main()
{
    {
        char out[24] = "x";
        wxerror::ErrorOf(nullptr, 0, out, sizeof out);
        const bool a = out[0] == 0;
        const uint8_t body[4] = {'{', '"', 'e', '"'};
        std::strcpy(out, "x");
        wxerror::ErrorOf(body, 0, out, sizeof out);
        const bool b = out[0] == 0;
        check(a && b && wxsetup::RefusalOf(403, out) == Refusal::Other, "ERR-NOBODY",
              "no body: no reason, and a 403 is Other (ACCESS REFUSED), never a guessed kind");
    }
    {
        const std::string t = Read("{\"error\":\"forbidden\"}");
        check(t == "forbidden" && wxsetup::RefusalOf(403, t.c_str()) == Refusal::NotActivated, "ERR-FORBIDDEN",
              "'" + t + "' -> NOT ACTIVATED YET");
    }
    {
        const std::string t = Read("{\"ok\":false,\"error\":\"fake_device_id\"}");
        check(t == "fake_device_id" && wxsetup::RefusalOf(403, t.c_str()) == Refusal::DeviceUnknown, "ERR-FAKE",
              "'" + t + "' -> BOARD NOT RECOGNISED");
    }
    {
        const char* bad[] = {"{\"error\":\"forbid", "{\"err\":\"forbidden\"}", "{\"error\": \"forbidden\"}",
                             "{\"error\":\"\"}", "{\"error\":\"for\\\"bidden\"}"};
        int read = 0;
        for (const char* b : bad) read += !Read(b).empty();
        check(read == 0 && wxsetup::RefusalOf(403, "") == Refusal::Other, "ERR-MALFORMED",
              "5 malformed bodies, " + std::to_string(read) + " read as a reason");
    }
    {
        const std::string longTok(30, 'f');
        const std::string body = "{\"error\":\"" + longTok + "\"}";
        const std::string fits(23, 'g');
        const std::string body2 = "{\"error\":\"" + fits + "\"}";
        const std::string a = Read(body.c_str(), 24), b = Read(body2.c_str(), 24);
        check(a.empty() && b == fits, "ERR-LONG",
              "a 30-byte token in a 24-byte buffer is refused whole (" + std::to_string(a.size()) +
                  " bytes read); a 23-byte one fits (" + std::to_string(b.size()) + ")");
    }
    {
        check(wxsetup::RefusalOf(401, "") == Refusal::KeyRejected, "ERR-401",
              "a 401 needs no body: KEY NOT ACCEPTED by its status alone");
    }

    std::printf("%s\n", failures ? "WX ERROR BODY TESTS FAILED" : "all WX error body checks passed");
    return failures ? 1 : 0;
}
