// Host test for the detail card's absence copy.
//
// THE DIGIT RULE IS THE POINT, and it is asserted rather than left as a comment
// beside the strings. The Follow ocean copy has the same guard for the same
// reason: a comment saying "do not add a time here" is advice sitting next to a
// string, addressed to someone who has already decided the string looks
// unfinished. A test that fails the build does not ask anyone to agree.
//
// Any restored precision -- a time, an altitude, a count, in any wording and any
// unit -- contains a digit. So the check does not have to anticipate the
// sentence somebody might add.
//
// AND THE CONTROL IS NOT OPTIONAL. "Contains no digit" is trivially satisfied by
// an empty string, and an empty explanation is a worse failure than an invented
// number: the card would go back to rendering absence as blank space, which is
// the defect this copy exists to fix. So every case that must explain itself is
// also asserted to actually say something.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../include/CardCopy.h"

static int failures = 0;

static void check(bool cond, const char* what)
{
    if (!cond) { printf("  FAIL  %s\n", what); ++failures; }
    else       { printf("  ok    %s\n", what); }
}

static bool hasDigit(const char* s)
{
    for (const char* p = s; p && *p; ++p)
        if (*p >= '0' && *p <= '9') return true;
    return false;
}

int main()
{
    printf("card copy: absence lines\n");

    const char* out[cardcopy::MAX_LINES];

    // --- Relayed: the case the whole file exists for ---------------------
    {
        for (int i = 0; i < cardcopy::MAX_LINES; ++i) out[i] = nullptr;
        const int n = cardcopy::Lines(cardcopy::Absence::Relayed, out);
        check(n > 0, "CONTROL: a relayed contact says SOMETHING");
        bool digits = false, empty = false;
        for (int i = 0; i < n; ++i) {
            if (out[i] == nullptr || out[i][0] == '\0') empty = true;
            if (hasDigit(out[i])) digits = true;
        }
        check(!digits, "relayed copy states no number the device cannot know");
        check(!empty, "CONTROL: no line is blank");

        // The first line is the one a cramped card keeps, so it must carry the
        // word that answers the question rather than merely introducing it.
        check(n >= 1 && std::strstr(out[0], "RELAY") != nullptr,
              "the first line names the cause, because it is the one that survives");
    }

    // --- NotFound: a different fact, and it must READ differently ---------
    {
        const char* nf[cardcopy::MAX_LINES];
        for (int i = 0; i < cardcopy::MAX_LINES; ++i) nf[i] = nullptr;
        const int n = cardcopy::Lines(cardcopy::Absence::NotFound, nf);
        check(n > 0, "CONTROL: a failed lookup says SOMETHING");
        bool digits = false;
        for (int i = 0; i < n; ++i) if (hasDigit(nf[i])) digits = true;
        check(!digits, "not-found copy states no number either");

        // The two absences are permanent and temporary respectively. If they
        // ever render the same text the card is back to one appearance for two
        // facts, which is the defect.
        const char* rl[cardcopy::MAX_LINES];
        cardcopy::Lines(cardcopy::Absence::Relayed, rl);
        check(std::strcmp(nf[0], rl[0]) != 0,
              "a relayed contact and a failed lookup do not read alike");
    }

    // --- None: an aircraft with identity explains nothing -----------------
    {
        for (int i = 0; i < cardcopy::MAX_LINES; ++i) out[i] = nullptr;
        const int n = cardcopy::Lines(cardcopy::Absence::None, out);
        check(n == 0, "an aircraft with details adds no explanation");
    }

    // --- the caller's buffer is never overrun -----------------------------
    {
        for (cardcopy::Absence a : { cardcopy::Absence::None,
                                     cardcopy::Absence::Relayed,
                                     cardcopy::Absence::NotFound }) {
            const char* buf[cardcopy::MAX_LINES];
            const int n = cardcopy::Lines(a, buf);
            if (n < 0 || n > cardcopy::MAX_LINES) {
                printf("  FAIL  Lines() returned %d, outside 0..%d\n", n, cardcopy::MAX_LINES);
                ++failures;
            }
        }
        check(true, "every case returns a count within the buffer");
    }

    printf(failures ? "\ncard copy: %d FAILURE(S)\n" : "\ncard copy: all good\n", failures);
    return failures ? 1 : 0;
}
