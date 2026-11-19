#pragma once
// The proxy's refusal reason, read from a 401/403 body: {"error":"<token>"} (docs/weatherscope-stage4.md S4.43). Pure;
// host-tested in test/host/test_wx_errorbody.cpp, its sabotages test/host/sabotage_wx_errorbody.py.
//
// THE RULE: a reason is read only from a body that holds one whole. Anything else -- no body (HttpRequestManager's
// GetBytes reads none on a non-200 today, S4.43), a malformed one, or a token longer than `cap` -- yields "", which
// wxsetup::RefusalOf maps to Other: ACCESS REFUSED. Never a guessed kind, and never a truncated one: half a token is a
// different token.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wxerror {

inline void ErrorOf(const uint8_t* body, size_t len, char* out, size_t cap)
{
    if (cap == 0) return;
    out[0] = '\0';
    if (body == nullptr || len == 0) return;
    static const char kKey[] = "\"error\":\"";
    const size_t k = sizeof kKey - 1;
    for (size_t i = 0; i + k <= len; ++i) {
        if (memcmp(body + i, kKey, k) != 0) continue;
        const size_t start = i + k;
        for (size_t j = start; j < len; ++j) {
            if (body[j] == '"') {
                const size_t n = j - start;
                if (n == 0 || n >= cap) return;   // empty, or too long for `cap`: refused whole, never cut
                memcpy(out, body + start, n);
                out[n] = '\0';
                return;
            }
            if (body[j] == '\\' || body[j] < 0x20) return;   // an escape or a control byte: not one of the proxy's tokens
        }
        return;   // no closing quote: malformed
    }
}

}  // namespace wxerror
